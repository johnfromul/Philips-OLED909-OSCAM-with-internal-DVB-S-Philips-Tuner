/* SPDX-License-Identifier: GPL-3.0-or-later */
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
import java.util.concurrent.TimeUnit;
import java.util.regex.*;

/** Root service for this TV's main DVB tuner; no ActivityThread/system context. */
public final class BridgeService {
    private static final String ROOT = "/data/local/tmp/oscam-mtk";
    private static final String INPUT = "com.mediatek.dtv.tvinput.dvbtuner/.DvbTvInputService/HW0";
    private static volatile Running running;
    private static volatile boolean stopping;
    private static final SimpleDateFormat DATE = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT);

    private static synchronized void log(String message) {
        try {
            File file = new File(ROOT, "bridge.log");
            if (file.length() >= 512 * 1024) {
                File previous = new File(ROOT, "bridge.log.1");
                if (previous.exists() && !previous.delete()) throw new IOException("Cannot rotate previous log");
                if (!file.renameTo(previous)) throw new IOException("Cannot rotate log");
            }
            try (PrintWriter out = new PrintWriter(new FileOutputStream(file, true))) {
                out.println(DATE.format(new Date()) + " " + message);
            }
        } catch (IOException error) { System.err.println("BRIDGE_LOG_ERROR " + error.getClass().getSimpleName()); }
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
    private static int[] metadata(String channel) throws Exception {
        Process query = new ProcessBuilder("/system/bin/content", "query", "--uri", channel,
                "--projection", "service_id:transport_stream_id").redirectErrorStream(true).start();
        if (!query.waitFor(4, TimeUnit.SECONDS)) {
            query.destroyForcibly();query.waitFor();throw new IOException("Channel query timed out");
        }
        String text = new String(query.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        Matcher sid = Pattern.compile("(?:^|[ ,])service_id=([0-9]+)(?:,|$)").matcher(text.trim());
        Matcher ts = Pattern.compile("(?:^|[ ,])transport_stream_id=([0-9]+)(?:,|$)").matcher(text.trim());
        if (query.exitValue()!=0 || !sid.find() || !ts.find()) throw new IOException("Channel metadata unavailable");
        int service = Integer.parseInt(sid.group(1)), transport = Integer.parseInt(ts.group(1));
        if (service<=0 || service>65535 || transport<=0 || transport>65535) throw new IOException("Invalid DVB service");
        return new int[]{service, transport};
    }
    private static final class Running implements AutoCloseable {
        final MediaCas cas;
        final MediaCas.Session session;
        final Process child;
        final Thread output;
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
                    String line;while((line=reader.readLine())!=null)log(line);
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
        if(previous!=null)previous.close();
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
            log("SUPERVISOR_START");
            String selected=null;long retryAt=0;int failures=0;boolean queryFailed=false;
            while(!stopping) {
                try {
                    String channel=currentChannel();queryFailed=false;
                    if(!Objects.equals(selected,channel)) {
                        stopChild();selected=channel;retryAt=SystemClock.elapsedRealtime()+500;failures=0;
                        log("CHANNEL "+(channel==null?"idle":channel));
                    }
                    if(running!=null && !running.child.isAlive()) {
                        int status=running.child.exitValue();stopChild();
                        failures++;retryAt=SystemClock.elapsedRealtime()+(status==20?60000:Math.min(10000,failures*2000));
                        log("CHILD_EXIT status="+status);
                    }
                    if(selected!=null && running==null && SystemClock.elapsedRealtime()>=retryAt) {
                        int[] info=metadata(selected);
                        // Query again after the external metadata call: never start a stale channel.
                        if(Objects.equals(selected,currentChannel())) {
                            log("START service="+info[0]+" tsid="+info[1]);
                            startChild(info[0],info[1]);
                        }
                    }
                } catch(Exception error) {
                    stopChild();
                    if(!queryFailed)log("RETRY "+error.getClass().getSimpleName());
                    queryFailed=true;retryAt=SystemClock.elapsedRealtime()+3000;
                    SystemClock.sleep(1000);
                }
                SystemClock.sleep(400);
            }
            stopChild();lock.release();
        }
    }
}
