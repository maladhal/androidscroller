package com.example.scroller;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import android.content.Context;

public class NetworkHelper {
    
    private static Context appContext;
    
    public static void setContext(Context context) {
        appContext = context.getApplicationContext();
    }
    
    public static String downloadText(String urlString) {
        try {
            URL url = new URL(urlString);
            HttpURLConnection connection = (HttpURLConnection) url.openConnection();
            connection.setRequestMethod("GET");
            connection.setConnectTimeout(5000);
            connection.setReadTimeout(10000);
            
            BufferedReader reader = new BufferedReader(new InputStreamReader(connection.getInputStream()));
            StringBuilder result = new StringBuilder();
            String line;
            
            while ((line = reader.readLine()) != null) {
                result.append(line).append("\n");
            }
            
            reader.close();
            connection.disconnect();
            
            return result.toString();
            
        } catch (IOException e) {
            e.printStackTrace();
            return null;
        }
    }
    
    public static byte[] downloadBytes(String urlString) {
        try {
            URL url = new URL(urlString);
            HttpURLConnection connection = (HttpURLConnection) url.openConnection();
            connection.setRequestMethod("GET");
            connection.setConnectTimeout(5000);
            connection.setReadTimeout(10000);
            
            InputStream inputStream = connection.getInputStream();
            ByteArrayOutputStream outputStream = new ByteArrayOutputStream();
            
            byte[] buffer = new byte[4096];
            int bytesRead;
            
            while ((bytesRead = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, bytesRead);
            }
            
            inputStream.close();
            connection.disconnect();
            
            return outputStream.toByteArray();
            
        } catch (IOException e) {
            e.printStackTrace();
            return null;
        }
    }
    
    public static String downloadImageToFile(String urlString, String filename) {
        if (appContext == null) {
            return null;
        }
        
        try {
            URL url = new URL(urlString);
            HttpURLConnection connection = (HttpURLConnection) url.openConnection();
            connection.setRequestMethod("GET");
            connection.setConnectTimeout(5000);
            connection.setReadTimeout(10000);
            
            InputStream inputStream = connection.getInputStream();
            
            // Save to internal storage
            File filesDir = appContext.getFilesDir();
            File imageFile = new File(filesDir, filename);
            
            FileOutputStream outputStream = new FileOutputStream(imageFile);
            
            byte[] buffer = new byte[4096];
            int bytesRead;
            
            while ((bytesRead = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, bytesRead);
            }
            
            inputStream.close();
            outputStream.close();
            connection.disconnect();
            
            return imageFile.getAbsolutePath();
            
        } catch (IOException e) {
            e.printStackTrace();
            return null;
        }
    }
    
    public static byte[] downloadImageData(String urlString) {
        try {
            URL url = new URL(urlString);
            HttpURLConnection connection = (HttpURLConnection) url.openConnection();
            connection.setRequestMethod("GET");
            connection.setConnectTimeout(5000);
            connection.setReadTimeout(10000);
            
            InputStream inputStream = connection.getInputStream();
            ByteArrayOutputStream outputStream = new ByteArrayOutputStream();
            
            byte[] buffer = new byte[4096];
            int bytesRead;
            
            while ((bytesRead = inputStream.read(buffer)) != -1) {
                outputStream.write(buffer, 0, bytesRead);
            }
            
            inputStream.close();
            connection.disconnect();
            
            return outputStream.toByteArray();
            
        } catch (IOException e) {
            e.printStackTrace();
            return null;
        }
    }
}