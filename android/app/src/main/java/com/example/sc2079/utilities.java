package com.example.sc2079;

import android.util.Log;
import android.widget.EditText;

import org.json.JSONException;
import org.json.JSONObject;


public class utilities {
    public static final String rotateRightString = "tr";
    public static final String rotateLeftString = "tl";
    public static final String strafeRightString = "<FR090>";
    public static final String strafeLeftString = "<FL090>";
    public static final String forwardString = "<FW010>";
    public static final String reverseString = "<BW010>";
    public static final String reverseLeftString = "<BL090>";
    public static final String reverseRightString = "<BR090>";
    public static final String beginExplorationString = "beginExplore";
    public static final String beginFastestString = "beginFastest";
    public static final String sendArenaString = "sendArena";

    public static final String sendStichSignal = "stitch";
    public String vehiclePoint;
    public String arenaData;

    public void setVehiclePoint(String vehiclePoint){
        this.vehiclePoint = vehiclePoint;
    }

    public String getJsonCraftVehicleMovement() {
        return "{\"cat\": \"stm\", \"value\": \"" + this.vehiclePoint + "\"}";
    }

    public String getJsonCraftSendArena(){
        return "{\"cat\": \"sendArena\", \"value\": " + this.arenaData + "}";
    }

    public String getJsonCraftSendStichSignal(){
        return "{\"cat\": \"stitch-image\", \"value\": \"" + sendStichSignal + "\"}";
    }
    public static String convertBooleanToString(boolean b){
        return b ? "true" : "false";
    }

    public static Boolean convertStringToBoolean(String s){
        return Boolean.parseBoolean(s);
    }

    public static String convertIntToString(int b) {
        return String.valueOf(b);
    }

    public static String convertStringToInt(String b){
        return String.valueOf(b);
    }

    public static int convertEditTextToInt(EditText b){
        return Integer.parseInt(b.getText().toString().trim());
    }

    public static JSONObject convertStringToJson(String message) throws JSONException {
            try {
                // Replace single quotes with double quotes for valid JSON
                String cleaned = message.replace("'", "\"");
                return new JSONObject(cleaned);
            } catch (Exception e) {
                // Fallback: wrap plain text
                JSONObject json = new JSONObject();
                json.put("message", message);
                return json;
            }
    }

    public static int returnMiddleSTMValues(String msg) {
        Log.d("GridMapClass.java", "Message:"+msg);
        String onlyNum = msg.replaceAll("\\D", "");

        if (onlyNum.length() < 3) {
            return Integer.parseInt(onlyNum);
        }

        int mid = onlyNum.length() / 2;
        int start = mid - 1;
        int end = mid + 2;

        String middleThree = onlyNum.substring(start, end);
        Log.d("STM Movement Record", middleThree);
        return Integer.parseInt(middleThree);
    }

    public static String returnFirstTwoSTMValue(String msg) {
        if (msg == null || msg.isEmpty()) {
            return "";
        }

        int start = msg.indexOf('<');
        int end = msg.indexOf('>');

        if (start != -1 && end != -1 && end - start >= 3) {
            Log.d("STM Movement Record", msg.substring(start + 1, start + 3));
            return msg.substring(start + 1, start + 3);
        }

        return "";
    }

    public static String convertObstacleIdToObstacleString(int idToConvert){
        switch(idToConvert){
            case 11:
                return "1";

            case 12:
                return "2";

            case 13:
                return "3";

            case 14:
                return "4";

            case 15:
                return "5";

            case 16:
                return "6";

            case 17:
                return "7";

            case 18:
                return "8";

            case 19:
                return "9";

            case 20:
                return "A";

            case 21:
                return "B";

            case 22:
                return "C";

            case 23:
                return "D";

            case 24:
                return "E";

            case 25:
                return "F";

            case 26:
                return "G";

            case 27:
                return "H";

            case 28:
                return "S";

            case 29:
                return "T";

            case 30:
                return "U";

            case 31:
                return "V";

            case 32:
                return "W";

            case 33:
                return "X";

            case 34:
                return "Y";

            case 35:
                return "Z";

            case 36:
                return "⬆";

            case 37:
                return "⬇";

            case 38:
                return "➡";

            case 39:
                return "⬅";

            case 40:
                return "O";
            default:
                return String.valueOf(idToConvert);
        }
    }
}
