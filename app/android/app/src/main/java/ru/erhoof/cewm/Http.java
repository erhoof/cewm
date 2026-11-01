package ru.erhoof.cewm;

import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.List;
import java.util.Map;

/** HTTP(S) client for the native side (the Android build has no libcurl); see android/curl_jni.cpp. */
public class Http {
    public int code;
    public String url;      // after redirects
    public String headers;  // every hop: status line + header lines, CRLF-separated
    public long length;     // Content-Length of the final response, -1 if unknown
    private HttpURLConnection c;
    private InputStream in;

    public static Http open(String url, String method, String[] hdrs, byte[] body, boolean follow, int connectMs, int readMs) throws IOException {
        Http h = new Http();
        StringBuilder all = new StringBuilder();
        for (int hop = 0; hop < 11; hop++) {
            HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
            c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(connectMs);
            c.setReadTimeout(readMs);
            c.setRequestMethod(method);
            for (String s : hdrs) {
                int i = s.indexOf(':');
                if (i > 0) c.setRequestProperty(s.substring(0, i).trim(), s.substring(i + 1).trim());
            }
            if (body != null && method.equals("POST")) {
                c.setDoOutput(true);
                c.getOutputStream().write(body);
            }
            h.code = c.getResponseCode();
            all.append(c.getHeaderField(null)).append("\r\n");
            for (Map.Entry<String, List<String>> e : c.getHeaderFields().entrySet())
                if (e.getKey() != null) for (String v : e.getValue()) all.append(e.getKey()).append(": ").append(v).append("\r\n");
            all.append("\r\n");
            String loc = c.getHeaderField("Location");
            if (follow && h.code >= 300 && h.code < 400 && loc != null) {
                url = new URL(new URL(url), loc).toString();
                if (h.code != 307 && h.code != 308) { method = method.equals("HEAD") ? "HEAD" : "GET"; body = null; }
                c.disconnect();
                continue;
            }
            h.c = c;
            h.url = url;
            h.headers = all.toString();
            h.length = c.getContentLengthLong();
            h.in = h.code >= 400 ? c.getErrorStream() : (method.equals("HEAD") ? null : c.getInputStream());
            return h;
        }
        throw new IOException("too many redirects");
    }

    /** Bytes read, -1 at the end, -2 on an error. */
    public int read(byte[] b) {
        if (in == null) return -1;
        try { return in.read(b); } catch (IOException e) { return -2; }
    }

    public void close() { if (c != null) c.disconnect(); }
}
