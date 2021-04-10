package com.bhj.setclip;

import android.app.ActivityManager.RunningTaskInfo;
import android.app.ActivityManager;
import android.app.Notification;
import android.app.Service;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentProvider;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Environment;
import android.os.IBinder;
import android.provider.ContactsContract.CommonDataKinds.Phone;
import android.provider.ContactsContract.Contacts.Entity;
import android.provider.ContactsContract.Data;
import android.provider.ContactsContract.RawContacts;
import android.provider.ContactsContract;
import android.provider.MediaStore;
import android.telephony.TelephonyManager;
import android.util.Log;
import android.webkit.MimeTypeMap;
import android.widget.Toast;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.FilenameFilter;
import java.io.IOException;
import java.io.Writer;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.regex.Pattern;

public class PutClipService extends Service implements MediaScannerConnection.MediaScannerConnectionClient {
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate()  {
        super.onCreate();
        startForeground(1, new Notification());
        mClipboard = (ClipboardManager)getSystemService(CLIPBOARD_SERVICE);
    }

    private static String myClipStr;

    private String getTask() {
        ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        RunningTaskInfo foregroundTaskInfo = am.getRunningTasks(1).get(0);
        return foregroundTaskInfo .topActivity.getPackageName();
    }

    private void writeFile(String str) throws IOException {
        FileWriter f = new FileWriter(new File(sdcard, "putclip.txt.1"));
        f.write(str);
        f.close();
        File txt = new File(sdcard, "putclip.txt.1");
        txt.renameTo(new File(sdcard, "putclip.txt"));

        writeFile(str, new File(sdcard, "putclip.txt.1"));
    }

    private void writeFile(String str, File file) throws IOException {
        FileWriter f = new FileWriter(file);
        f.write(str);
        f.close();
        File txt = new File(sdcard, "putclip.txt.1");
        txt.renameTo(new File(sdcard, "putclip.txt"));
    }

    ClipboardManager mClipboard;
    ClipboardManager.OnPrimaryClipChangedListener mClipListner;
    boolean mClipboardHooked = false;

    boolean mWatchingClipboard = true;

    private void startMonitorClipboard() {
        mWatchingClipboard = true;

        if (mClipboardHooked) {
            return;
        }

        if (mClipListner == null) {
            mClipListner = new ClipboardManager.OnPrimaryClipChangedListener() {
                    public void onPrimaryClipChanged() {
                        if (!mWatchingClipboard) {
                            return;
                        }

                        if (! new File(sdcard, "MobileOrg").isDirectory()) {
                            mWatchingClipboard = false;
                            return;
                        }

                        ClipData clip = mClipboard.getPrimaryClip();
                        if (clip != null) {
                            ClipData.Item item = clip.getItemAt(0);
                            CharSequence text = item.getText();
                            if (text == null) {
                                return;
                            }

                            String str = text.toString();

                            if (!str.equals(PutClipService.myClipStr) && str.length() < 50) {
                                // * F(edit:addheading) [[olp:jwords.org][jwords.org]]
                                // ** Old value

                                // ** New value
                                // TODO hh
                                // [2017-08-22 周二 22:13]
                                // ** End of edit

                                try {
                                    FileWriter mobileOrgWriter =
                                        new FileWriter(new File(sdcard, "MobileOrg/mobileorg.org"), true);

                                    Date now = new Date();
                                    mobileOrgWriter.write(
                                        "\n\n* F(edit:addheading) [[olp:jwords.org][jwords.org]]\n" +
                                        "** Old value\n\n" +
                                        "** New value\n" +
                                        "TODO " +
                                        str +
                                        "\n[" + now.toString() + "]\n" +
                                        "** End of edit\n");
                                    mobileOrgWriter.close();
                                } catch (IOException e) {
                                    Log.e("bhj", String.format("%s:%d: can't write org-mode", "PutClipService.java", 134), e);
                                }

                            }
                        }
                    }
                };
        }
        if (!mClipboardHooked) {
            mClipboard.addPrimaryClipChangedListener(mClipListner);
            mClipboardHooked = true;
        }
    }

    private String readAndDelete(String fileName) throws java.io.FileNotFoundException, java.io.IOException {
        File file = new File(sdcard, fileName);
        FileReader f = new FileReader(file);
        char[] buffer = new char[1024 * 1024];
        int n = f.read(buffer);
        f.close();
        file.delete();
        if (n >= 0) {
            String str = new String(buffer, 0, n);
            return str;
        } else {
            return "";
        }
    }

    private List<ResolveInfo> mApps;

    private boolean saveBitmapToFile(File dir, String fileName, Bitmap bm,
                                     Bitmap.CompressFormat format, int quality) {

        File imageFile = new File(dir,fileName);

        FileOutputStream fos = null;
        try {
            fos = new FileOutputStream(imageFile);

            bm.compress(format,quality,fos);

            fos.close();

            return true;
        }
        catch (IOException e) {
            Log.e("app",e.getMessage());
            if (fos != null) {
                try {
                    fos.close();
                } catch (IOException e1) {
                    e1.printStackTrace();
                }
            }
        }
        return false;
    }



    private static Bitmap drawableToBitmap (Drawable drawable) {
        Bitmap bitmap = null;

        if (drawable instanceof BitmapDrawable) {
            BitmapDrawable bitmapDrawable = (BitmapDrawable) drawable;
            if(bitmapDrawable.getBitmap() != null) {
                return bitmapDrawable.getBitmap();
            }
        }

        if(drawable.getIntrinsicWidth() <= 0 || drawable.getIntrinsicHeight() <= 0) {
            bitmap = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888); // Single color bitmap will be created of 1x1 pixel
        } else {
            bitmap = Bitmap.createBitmap(drawable.getIntrinsicWidth(), drawable.getIntrinsicHeight(), Bitmap.Config.ARGB_8888);
        }

        Canvas canvas = new Canvas(bitmap);
        drawable.setBounds(0, 0, canvas.getWidth(), canvas.getHeight());
        drawable.draw(canvas);
        return bitmap;
    }


    private void loadApps() throws IOException {
        Intent mainIntent = new Intent(Intent.ACTION_MAIN, null);
        mainIntent.addCategory(Intent.CATEGORY_LAUNCHER);

        mApps = getPackageManager().queryIntentActivities(mainIntent, 0);

        File wrenchDir = new File(sdcard, "Wrench");
        wrenchDir.mkdir();

        FileWriter appListFile = new FileWriter(new File(wrenchDir, "apps.txt"));

        for (int i = 0; i < mApps.size(); i++) {
            ResolveInfo info = mApps.get(i);
            appListFile.write(
                String.format("%s=%s=%s\n",
                              info.activityInfo.name,
                              info.activityInfo.packageName,
                              info.activityInfo.loadLabel(getPackageManager())));
            Drawable icon = info.activityInfo.loadIcon(getPackageManager());
            Bitmap bm = drawableToBitmap(icon);
            saveBitmapToFile(wrenchDir, String.format("%s.png", info.activityInfo.name), bm, Bitmap.CompressFormat.PNG, 100);
        }
        appListFile.close();
        new File(wrenchDir, "apps.txt").renameTo(new File(wrenchDir, "apps.info"));
    }

    MediaScannerConnection mMediaScanner;
    String mPicToScan;
    @Override
    public int onStartCommand(Intent intent,  int flags,  int startId)  {
        try {
            if (intent == null) {
                return START_STICKY;
            }

            String picName = intent.getStringExtra("picture");
            String toastText = intent.getStringExtra("toast");
            if (picName != null) {
                Uri picUri = Uri.parse("file://" + picName);
                if (mMediaScanner == null) {
                    mMediaScanner = new MediaScannerConnection(getApplicationContext(), this);
                    mMediaScanner.connect();
                    mPicToScan = picName;
                } else {
                    mMediaScanner.scanFile(picName, getMimeType(picName));
                }
                // sendBroadcast(new Intent(Intent.ACTION_MEDIA_SCANNER_SCAN_FILE, picUri));
            } else if (toastText != null) {
                Toast.makeText(PutClipService.this, String.format("%s", toastText), Toast.LENGTH_SHORT).show();
            } else if (intent.getIntExtra("watch-clipboard", 0) == 1) {
                startMonitorClipboard();
                try {
                    writeFile("hello", new File(sdcard, ".wrench-watching-clipboard.txt"));
                } catch (Throwable e) {
                    Log.e("bhj", String.format("%s:%d: ", "PutClipService.java", 122), e);
                }

            } else if (intent.getIntExtra("stop-watch-clipboard", 0) == 1) {
                mWatchingClipboard = false;
                new File(sdcard, ".wrench-watching-clipboard.txt").delete();
            } else if (intent.getIntExtra("gettask", 0) == 1) {
                String foregroundTaskPackageName = getTask();
                writeFile(foregroundTaskPackageName);
            } else if (intent.getIntExtra("getclip", 0) == 1) {
                String str = mClipboard.getPrimaryClip().getItemAt(0).getText().toString();
                writeFile(str);
            } else if (intent.getIntExtra("getphone", 0) == 1) {
                TelephonyManager tMgr =(TelephonyManager)getSystemService(Context.TELEPHONY_SERVICE);
                String mPhoneNumber = tMgr.getLine1Number();
                writeFile(mPhoneNumber);
            } else if (intent.getIntExtra("listapps", 0) == 1) {
                try {
                    loadApps();
                } catch (IOException e) {
                    Log.e("bhj", String.format("%s:%d: ", "PutClipService.java", 273), e);
                }

            } else if (intent.getIntExtra("listcontacts", 0) == 1) {
                String data2 = intent.getStringExtra("data2");
                String data3 = intent.getStringExtra("data3");

                if (data2 == null) {
                    data2 = "微信";
                }
                if (data3 == null) {
                    data3 = "发送消息";
                }

                ContentResolver resolver = getContentResolver();
                if (resolver == null) {
                    return START_STICKY;
                }

                String[] projection = new String[] {
                    Data.DATA1,
                };
                String selection = ContactsContract.Data.DATA2 + "=?" + " AND " +
                    ContactsContract.Data.DATA3 + "=?";
                String[] selectArgs = new String[] {
                    data2, data3,
                };

                Cursor dc = resolver.query(ContactsContract.Data.CONTENT_URI,
                                           projection,
                                           selection,
                                           selectArgs,
                                           null);

                try {
                    File txt = new File(sdcard, "listcontacts.txt.1");
                    FileWriter f = new FileWriter(txt);
                    if (dc.moveToFirst()) {
                        do {
                            f.write(normalizedPhone(dc.getString(0)) + "\n");
                        } while (dc.moveToNext());
                    }
                    f.close();
                    txt.renameTo(new File(sdcard, "listcontacts.txt"));
                } finally {
                    dc.close();
                }
            } else if (intent.getIntExtra("share-pics", 0) == 1) {
                String pkg = intent.getStringExtra("package");
                String cls = intent.getStringExtra("class");

                String pics = intent.getStringExtra("pics");
                String[] picsArray = pics.split(",");
                ArrayList<Uri> imageUris = new ArrayList<Uri>();
                for (String pic : picsArray) {
                    imageUris.add(Uri.fromFile(new File(pic)));
                }

                Intent shareIntent = new Intent();
                shareIntent.setClassName(pkg, cls);
                if (picsArray.length == 1) {
                    shareIntent.setAction(Intent.ACTION_SEND);
                    shareIntent.putExtra(Intent.EXTRA_STREAM, imageUris.get(0));
                } else {
                    shareIntent.setAction(Intent.ACTION_SEND_MULTIPLE);
                    shareIntent.putParcelableArrayListExtra(Intent.EXTRA_STREAM, imageUris);
                }
                shareIntent.setType("image/*");
                shareIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(shareIntent);
            } else if (intent.getIntExtra("share-text", 0) == 1) {
                String pkg = intent.getStringExtra("package");
                String cls = intent.getStringExtra("class");

                String text = readAndDelete("putclip.txt");
                Intent shareIntent = new Intent();
                shareIntent.setClassName(pkg, cls);
                shareIntent.setAction(Intent.ACTION_SEND);
                shareIntent.setType("text/plain");
                shareIntent.putExtra(Intent.EXTRA_TEXT, text);
                shareIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(shareIntent);
            } else if (intent.getIntExtra("get-last-note-pic", 0) == 1) {
                final File notesPicDir = new File(sdcard, "smartisan/notes/");
                final Pattern pngPattern = Pattern.compile(".*\\.png$");
                Log.e("bhj", String.format("%s:%d: ", "PutClipService.java", 235));
                File[] picFiles = notesPicDir.listFiles(new FilenameFilter() {
                        public boolean accept(File dir, String filename) {
                            Log.e("bhj", String.format("%s:%d: %s", "PutClipService.java", 238, filename));
                            if (dir == notesPicDir && pngPattern.matcher(filename).matches()) {
                                return true;
                            } else {
                                return false;
                            }
                        }
                    });
                if (picFiles.length > 0) {
                    long newestLastModified = picFiles[0].lastModified();
                    File newestFile = picFiles[0];
                    for (File f : picFiles) {
                        if (newestLastModified < f.lastModified()) {
                            newestLastModified = f.lastModified();
                            newestFile = f;
                        }
                    }
                    writeFile(newestFile.toString());
                }
            } else if (intent.getIntExtra("share-to-note", 0) == 1) {
                String str = readAndDelete("putclip.txt");

                Intent notesIntent = new Intent();
                notesIntent.setClassName("com.smartisanos.notes", "com.smartisanos.notes.NotesActivity");
                notesIntent.setAction(Intent.ACTION_SEND);
                notesIntent.setType("text/plain");
                notesIntent.putExtra(Intent.EXTRA_TEXT, str);
                notesIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(notesIntent);
            } else if (intent.getIntExtra("getapk", 0) == 1) {
                try {
                    PackageManager pm = getPackageManager();
                    PackageInfo pi = pm.getPackageInfo("com.bhj.setclip", 0);

                    FileWriter f = new FileWriter(new File(sdcard, "setclip-apk.txt.1"));
                    f.write(String.format("if test -e %s; then CLASSPATH=%s LD_LIBRARY_PATH=%s:/system/app/miui/lib/arm64/ \"$@\"; else echo %s not found; fi", pi.applicationInfo.sourceDir, pi.applicationInfo.sourceDir, pi.applicationInfo.nativeLibraryDir, pi.applicationInfo.sourceDir));
                    f.close();
                    new File(sdcard, "setclip-apk.txt.1").renameTo(new File(sdcard, "setclip-apk.txt"));
                } catch (Throwable e) {
                    Toast.makeText(PutClipService.this, String.format("%s", "小扳手没有权限写 /sdcard/ 目录？新版安卓上需在系统设置中手动设置一下小扳手的权限"), Toast.LENGTH_LONG).show();
                    Log.e("bhj", String.format("%s:%d: ", "PutClipService.java", 134), e);
                }

                if (new File(sdcard, ".wrench-watching-clipboard.txt").exists()) {
                    startMonitorClipboard();
                } else if (new File(sdcard, "MobileOrg").isDirectory()) {
                    startMonitorClipboard();
                } else {
                    mWatchingClipboard = false;
                }

            } else if (intent.getIntExtra("getcontact", 0) == 1) {
                String contactNumber = intent.getStringExtra("contact");
                if (contactNumber == null) {
                    return START_STICKY;
                }

                ContentResolver resolver = getContentResolver();
                if (resolver == null) {
                    return START_STICKY;
                }

                // get the last 4 bytes of number, compare.
                // if not? compare the whole thing? or not?

                String last4 = "%" + contactNumber.substring(contactNumber.length() - 4);
                String selection = ContactsContract.Data.DATA2 + "=?" + " AND " +
                    ContactsContract.Data.DATA3 + "=?" + " AND " +
                    ContactsContract.Data.DATA1 + " like ?";

                Cursor dataCursor = resolver.query(ContactsContract.Data.CONTENT_URI,
                                                   new String[] {Data._ID, Data.DATA1, Data.DATA2, Data.DATA3},
                                                   selection,
                                                   new String[] {"微信", "发送消息", last4},
                                                   null);

                try {
                    if (dataCursor.moveToFirst()) {
                        do {
                            String phone = dataCursor.getString(1);
                            if (normalizedPhone(phone).equals(normalizedPhone(contactNumber))) {
                                Intent mmIntent = new Intent();
                                mmIntent.setClassName("com.tencent.mm", "com.tencent.mm.plugin.accountsync.ui.ContactsSyncUI");
                                mmIntent.setType("vnd.android.cursor.item/vnd.com.tencent.mm.chatting.profile");
                                mmIntent.setData(Uri.parse("content://com.android.contacts/data/" + dataCursor.getLong(0)));
                                mmIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                                startActivity(mmIntent);
                                break;
                            }
                        } while (dataCursor.moveToNext());
                    }
                } finally {
                    dataCursor.close();
                }
            } else {
                Log.e("bhj", String.format("%s:%d: ", "PutClipService.java • setclip", 487));
                myClipStr = readAndDelete("putclip.txt");
                mClipboard.setPrimaryClip(ClipData.newPlainText("Styled Text", myClipStr));
            }
        } catch (Exception e) {
            Log.e("bhj", String.format("%s:%d: ", "PutClipService.java", 77), e);
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
    }

    private String normalizedPhone(String phone) {
        String res = phone.replaceAll("[-+ ]", "");
        if (res.length() >= 11) {
            res = res.substring(res.length() - 11);
        }
        return res;
    }

    private static File sdcard = Environment.getExternalStorageDirectory();

    public static String getMimeType(String url) {
        String type = null;
        String extension = MimeTypeMap.getFileExtensionFromUrl(url);
        if (extension != null) {
            type = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);
        }
        return type;
    }

    @Override
    public void onMediaScannerConnected() {
        if (mPicToScan != null) {
            mMediaScanner.scanFile(mPicToScan, getMimeType(mPicToScan));
        }
    }

    @Override
    public void onScanCompleted(String s, Uri uri) {
        Log.e("bhj", String.format("%s:%d: onScanCompleted, %s %s", "PutClipService.java", 515, s, uri));
    }
}
