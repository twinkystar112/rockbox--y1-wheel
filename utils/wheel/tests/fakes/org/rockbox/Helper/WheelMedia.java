package org.rockbox.Helper;
/* test fake: the playing track for "love" */
public class WheelMedia {
    static String[] now;
    public static synchronized String[] currentArtistTitle() { return now; }
}
