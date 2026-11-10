package com.example.mcpemod;

import android.app.Activity;
import android.view.*;
import android.widget.*;

/** Activity-তে ভাসমান স্ক্রল মেনু বসায়। কল করো: MenuBuilder.show(activity); */
public class MenuBuilder {
    // নাম -> cmd নম্বর (mod.cpp-এর enum Cmd-এর ক্রম অনুযায়ী)
    static final Object[][] FEATURES = {
        {"Creative", 1}, {"Survival", 2},
        {"Day", 3}, {"Night", 4},
        {"Heal", 5}, {"Clear Effects", 6}, {"Extinguish Fire", 7},
        {"Jump Boost", 8}, {"Speed", 9}, {"Night Vision", 10}, {"Regeneration", 11},
        {"Resistance", 12}, {"Haste", 13}, {"Strength", 14}, {"Fire Resistance", 15},
        {"Water Breathing", 16}, {"Invisibility", 17},
        {"Fast Walk ON", 18}, {"Fast Walk OFF", 19},
        {"God Mode ON", 20}, {"God Mode OFF", 21},
    };

    public static void show(final Activity a) {
        a.runOnUiThread(new Runnable() { public void run() {
            LinearLayout col = new LinearLayout(a);
            col.setOrientation(LinearLayout.VERTICAL);
            for (final Object[] f : FEATURES) {
                Button b = new Button(a);
                b.setText((String) f[0]);
                b.setOnClickListener(new View.OnClickListener() {
                    public void onClick(View v) { Native.send((Integer) f[1]); }
                });
                col.addView(b);
            }
            ScrollView sv = new ScrollView(a);
            sv.addView(col);
            PopupWindow pw = new PopupWindow(sv, 500, 700);
            pw.setFocusable(false);
            pw.showAtLocation(a.getWindow().getDecorView(), Gravity.LEFT | Gravity.TOP, 20, 120);
        }});
    }
}
