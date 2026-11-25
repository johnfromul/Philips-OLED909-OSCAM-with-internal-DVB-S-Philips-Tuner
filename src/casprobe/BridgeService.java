package casprobe;

import android.media.MediaCas;
import android.net.Uri;
import android.os.IBinder;
import android.os.SystemClock;
import java.io.*;
import java.lang.reflect.Method;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.regex.*;

/** Root service for this TV's main DVB tuner; no ActivityThread/system context. */
public final class BridgeService {
    private static final String ROOT = "/data/local/tmp/oscam-mtk";
    private static final String INPUT = "com.mediatek.dtv.tvinput.dvbtuner/.DvbTvInputService/HW0";
    private static final int METADATA_CACHE_LIMIT = 64;
    private static final int METADATA_TIMEOUT_SECONDS = 6;
    private static final long OBSERVATION_GRACE_MS = 1500;
    private static final long PLAYBACK_RECOVERY_MS = 20000;
    private static final int EXIT_TUNING_TABLE_CHANGED = 10;
    private static final int EXIT_FREE_TO_AIR = 20;
    private static final int EXIT_CHANNEL_MISMATCH = 21;
    private static final Pattern SERVICE_ID =
            Pattern.compile("(?:^|[ ,])service_id=([0-9]+)(?:,|$)");
    private static final Pattern TRANSPORT_ID =
            Pattern.compile("(?:^|[ ,])transport_stream_id=([0-9]+)(?:,|$)");
    private static volatile Running running;
    private static volatile boolean stopping;
    private static final Object LOG_LOCK = new Object();
    private static final SimpleDateFormat DATE = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT);

    private static void log(String message) {
        // Waiting for the child output thread must not lock out its log writes.
        synchronized (LOG_LOCK) {
            try {
                File file = new File(ROOT, "bridge.log");
                if (file.length() >= 512 * 1024) {
                    File previous = new File(ROOT, "bridge.log.1");
                    if (previous.exists() && !previous.delete()) throw new IOException("Cannot rotate previous log");
                    if (!file.renameTo(previous)) throw new IOException("Cannot rotate log");
                }
                try (BufferedWriter out = new BufferedWriter(new FileWriter(file, true))) {
                    out.write(DATE.format(new Date()) + " " + message);
                    out.newLine();
                }
            } catch (IOException error) {
                System.err.println("BRIDGE_LOG_ERROR " + error.getClass().getSimpleName());
            }
        }
    }
    private static Object tvManager() throws Exception {
        IBinder binder = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("getService", String.class).invoke(null, "tv_input");
        if (binder == null) throw new IOException("TV input service unavailable");
        return Class.forName("android.media.tv.ITvInputManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null, binder);
    }
    private static String currentChannel() throws Exception {
        IBinder powerBinder = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("getService", String.class).invoke(null, "power");
        if(powerBinder==null)throw new IOException("Power service unavailable");
        Object power = Class.forName("android.os.IPowerManager$Stub")
                .getMethod("asInterface", IBinder.class).invoke(null,powerBinder);
        if(!(boolean)Class.forName("android.os.IPowerManager").getMethod("isInteractive").invoke(power))return null;
        Method query = Class.forName("android.media.tv.ITvInputManager")
                .getMethod("getCurrentTunedInfos", int.class);
        List<?> infos = (List<?>) query.invoke(tvManager(), 0);
        String channel = null;
        for (Object info : infos) {
            Class<?> type = info.getClass();
            if (!INPUT.equals(type.getMethod("getInputId").invoke(info)) ||
                (boolean) type.getMethod("isRecordingSession").invoke(info) ||
                !(boolean) type.getMethod("isMainSession").invoke(info)) continue;
            Uri uri = (Uri) type.getMethod("getChannelUri").invoke(info);
            if (uri == null || !uri.toString().matches("content://android\\.media\\.tv/channel/[0-9]+")) continue;
            if (channel != null) throw new IOException("Ambiguous main DVB session");
            channel = uri.toString();
        }
        return channel;
    }
    private static final class ChannelMetadata {
        final int service;
        final int transport;

        ChannelMetadata(int service, int transport) {
            this.service = service;
            this.transport = transport;
        }
    }

    private static final class MetadataFailure extends IOException {
        MetadataFailure(String reason) { super(reason); }
    }

    private static ChannelMetadata queryMetadata(String channel) throws IOException, InterruptedException {
        File capture = File.createTempFile("channel-query-", ".tmp", new File(ROOT));
        Process query = null;
        try {
            try {
                // A regular file lets the subprocess complete without waiting for a pipe reader.
                query = new ProcessBuilder("/system/bin/content", "query", "--uri", channel,
                        "--projection", "service_id:transport_stream_id")
                        .redirectErrorStream(true).redirectOutput(capture).start();
            } catch (IOException error) {
                throw new MetadataFailure("query_start_failed");
            }
            if (!query.waitFor(METADATA_TIMEOUT_SECONDS, TimeUnit.SECONDS)) {
                throw new MetadataFailure("query_timeout");
            }
            if (query.exitValue() != 0) {
                throw new MetadataFailure("query_exit_" + query.exitValue());
            }
            String row;
            try (InputStream output = new FileInputStream(capture)) {
                byte[] bytes = output.readNBytes(4097);
                if (bytes.length > 4096) throw new MetadataFailure("oversized_metadata_output");
                row = new String(bytes, StandardCharsets.UTF_8).trim();
            }
            Matcher sid = SERVICE_ID.matcher(row);
            Matcher ts = TRANSPORT_ID.matcher(row);
            if (!sid.find() || !ts.find()) throw new MetadataFailure("metadata_unavailable");
            int service;
            int transport;
            try {
                service = Integer.parseInt(sid.group(1));
                transport = Integer.parseInt(ts.group(1));
            } catch (NumberFormatException error) {
                throw new MetadataFailure("invalid_dvb_identifiers");
            }
            if (service <= 0 || service > 65535 || transport <= 0 || transport > 65535) {
                throw new MetadataFailure("invalid_dvb_identifiers");
            }
            return new ChannelMetadata(service, transport);
        } finally {
            if (query != null && query.isAlive()) query.destroyForcibly();
            if (!capture.delete()) log("METADATA_CAPTURE_CLEANUP_FAILED");
        }
    }
    private static final class Running implements AutoCloseable {
        final MediaCas cas;
        final MediaCas.Session session;
        final Process child;
        final Thread output;
        volatile long bridgeReadyAt;
        private boolean closed;
        Running(int sid, int tsid) throws Exception {
            cas = new MediaCas(6210);
            MediaCas.Session opened = null;Process started = null;
            try {
                opened = cas.openSession(0, 1);
                byte[] id = opened.getSessionId();
                if (id.length != 12) throw new IOException("Unexpected CAS session ABI");
                started = new ProcessBuilder(ROOT + "/mtk-bridge", "--hardware-from-cas-token",
                        Integer.toString(sid), "0", "0", Integer.toString(tsid))
                        .redirectErrorStream(true).start();
                try (OutputStream out = started.getOutputStream()) { out.write(id, 0, 8); }
                Arrays.fill(id, (byte) 0);
                session=opened;child=started;
            } catch (Exception failure) {
                if (started!=null) {
                    started.destroy();
                    if(!started.waitFor(4,TimeUnit.SECONDS)){started.destroyForcibly();started.waitFor();}
                }
                try {if (opened!=null) opened.close();} finally {cas.close();}
                throw failure;
            }
            output = new Thread(() -> {
                try (BufferedReader reader = new BufferedReader(new InputStreamReader(child.getInputStream(), StandardCharsets.UTF_8))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        if (bridgeReadyAt == 0 && line.startsWith("CW_RECEIVED ")) {
                            bridgeReadyAt = SystemClock.elapsedRealtime();
                        }
                        log(line);
                    }
                } catch (IOException error) { log("CHILD_LOG_CLOSED"); }
            }, "bridge-output");
            output.start();
        }
        public synchronized void close() {
            if(closed)return;
            closed=true;
            child.destroy();
            try {
                if (!child.waitFor(4,TimeUnit.SECONDS)) {
                    log("CHILD_STOP_TIMEOUT");child.destroyForcibly();child.waitFor();
                }
                output.join(1000);
                log("CHILD_STOP status="+child.exitValue());
            } catch (InterruptedException error) { Thread.currentThread().interrupt(); }
            try {session.close();} finally {cas.close();}
        }
    }
    private static synchronized void stopChild() {
        Running previous=running;running=null;
        if (previous != null) {
            try {
                previous.close();
            } catch (RuntimeException error) {
                log("CHILD_CLEANUP_FAILED error=" + error.getClass().getSimpleName());
            }
        }
    }
    private static synchronized void startChild(int service,int transport) throws Exception {
        if(!stopping)running=new Running(service,transport);
    }
    public static void main(String[] args) throws Exception {
        DATE.setTimeZone(TimeZone.getTimeZone("Europe/Prague"));
        try (RandomAccessFile lockFile = new RandomAccessFile(ROOT+"/supervisor.lock", "rw")) {
            FileLock lock = lockFile.getChannel().tryLock();
            if(lock==null) {System.err.println("SUPERVISOR_ALREADY_RUNNING");System.exit(73);return;}
            Runtime.getRuntime().addShutdownHook(new Thread(() -> {stopping=true;stopChild();}));
            try (PrintWriter pid = new PrintWriter(ROOT+"/supervisor.pid")) {pid.println(android.os.Process.myPid());}
            log("SUPERVISOR_START metadata_cache=1 async_metadata=1");
            Map<String, ChannelMetadata> channelMetadata = new LinkedHashMap<>(16, 0.75f, true);
            ExecutorService metadataWorker = Executors.newSingleThreadExecutor();
            Future<ChannelMetadata> lookup = null;
            long lookupStartedAt = 0;
            long failedObservationAt = -1;
            String selected = null;
            long retryAt = 0;
            int failures = 0;
            int metadataFailures = 0;
            try {
            while(!stopping) {
                String channel;
                try {
                    channel = currentChannel();
                    failedObservationAt = -1;
                } catch (Exception error) {
                    long now = SystemClock.elapsedRealtime();
                    if (failedObservationAt < 0) {
                        failedObservationAt = now;
                        log("CHANNEL_OBSERVATION_FAILED error=" + error.getClass().getSimpleName());
                    }
                    if (now - failedObservationAt >= OBSERVATION_GRACE_MS) {
                        if (lookup != null) {
                            lookup.cancel(true);
                            lookup = null;
                        }
                        if (running != null) {
                            log("CHANNEL_OBSERVATION_EXPIRED");
                            stopChild();
                        }
                    }
                    SystemClock.sleep(400);
                    continue;
                }
                String phase = "channel_change";
                try {
                    if(!Objects.equals(selected,channel)) {
                        if (lookup != null) {
                            lookup.cancel(true);
                            lookup = null;
                        }
                        stopChild();selected=channel;retryAt=SystemClock.elapsedRealtime()+500;failures=0;
                        metadataFailures = 0;
                        log("CHANNEL "+(channel==null?"idle":channel));
                    }
                    Running active = running;
                    if (active != null && active.bridgeReadyAt > 0
                            && SystemClock.elapsedRealtime() - active.bridgeReadyAt >= PLAYBACK_RECOVERY_MS) {
                        failures = 0;
                    }
                    if(active!=null && !active.child.isAlive()) {
                        phase = "cleanup_child";
                        int status=active.child.exitValue();stopChild();
                        if (status == EXIT_TUNING_TABLE_CHANGED || status == EXIT_CHANNEL_MISMATCH) {
                            channelMetadata.remove(selected);
                        }
                        long delay = status == EXIT_FREE_TO_AIR ? 60000 : 2000L * Math.min(++failures, 5);
                        retryAt = SystemClock.elapsedRealtime() + delay;
                        log("CHILD_EXIT status="+status);
                    }
                    if(selected!=null && running==null && SystemClock.elapsedRealtime()>=retryAt) {
                        phase = "metadata_query";
                        ChannelMetadata info = channelMetadata.get(selected);
                        boolean cacheHit = info != null;
                        if (!cacheHit) {
                            if (lookup == null) {
                                String requested = selected;
                                lookupStartedAt = SystemClock.elapsedRealtime();
                                lookup = metadataWorker.submit(() -> queryMetadata(requested));
                            }
                            if (!lookup.isDone()) {
                                SystemClock.sleep(400);
                                continue;
                            }
                            try {
                                info = lookup.get();
                            } catch (ExecutionException error) {
                                long now = SystemClock.elapsedRealtime();
                                metadataFailures = Math.min(metadataFailures + 1, 6);
                                long delay = 800L * metadataFailures;
                                retryAt = now + delay;
                                Throwable cause = error.getCause();
                                String reason = cause instanceof MetadataFailure ? cause.getMessage() : "query_io_error";
                                log("RETRY phase=metadata_query reason=" + reason + " elapsed_ms=" +
                                        (now - lookupStartedAt) + " retry_ms=" + delay + " channel=" + selected);
                                SystemClock.sleep(400);
                                continue;
                            } finally {
                                lookup = null;
                            }
                            channelMetadata.put(selected, info);
                            if (channelMetadata.size() > METADATA_CACHE_LIMIT) {
                                channelMetadata.remove(channelMetadata.keySet().iterator().next());
                            }
                        }
                        metadataFailures = 0;
                        long elapsed = cacheHit ? 0 : SystemClock.elapsedRealtime() - lookupStartedAt;
                        log("CHANNEL_METADATA source=" + (cacheHit ? "cache" : "provider") +
                                " elapsed_ms=" + elapsed + " channel=" + selected);
                        // Recheck the selection after provider I/O even on a cached lookup.
                        phase = "confirm_channel";
                        if(Objects.equals(selected,currentChannel())) {
                            phase = "start_bridge";
                            log("START service="+info.service+" tsid="+info.transport);
                            startChild(info.service,info.transport);
                        }
                    }
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    break;
                } catch(Exception error) {
                    log("RETRY phase="+phase+" error="+error.getClass().getSimpleName());
                    retryAt=SystemClock.elapsedRealtime()+3000;
                }
                SystemClock.sleep(400);
            }
            } finally {
                if (lookup != null) lookup.cancel(true);
                metadataWorker.shutdownNow();
                stopChild();
                lock.release();
            }
        }
    }
}
