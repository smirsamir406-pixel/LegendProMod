package com.example.mcpemod;

public class Native {
    static { System.loadLibrary("mcpemod"); }
    public static native void send(int cmd);
}
