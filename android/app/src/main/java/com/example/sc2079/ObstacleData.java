package com.example.sc2079;

import android.util.Log;

public class ObstacleData{
    public enum Direction{
        NORTH(1),
        SOUTH(3),
        EAST(2),
        WEST(4),
        EMPTY(0);

        private final int directionCode;

        Direction(int directionCode){
            this.directionCode = directionCode;
        }

        public int getIntFromDirection(){
            return this.directionCode;
        }

        public static Direction getDirectionFromInt(int directionCode){
            for (Direction d : Direction.values()){
                if (d.directionCode == directionCode){
                    return d;
                }
            }
            return EMPTY;
        }
        public static String getStringFromDirection(ObstacleData.Direction direction) {
            switch (direction) {
                case NORTH: return "NORTH";
                case SOUTH: return "SOUTH";
                case EAST:  return "EAST";
                case WEST:  return "WEST";
                default:    return "EMPTY";
            }
        }
        public static Direction getOppositeDirection(Direction direction) {
            switch (direction) {
                case NORTH: return SOUTH;
                case SOUTH: return NORTH;
                case EAST:  return WEST;
                case WEST:  return EAST;
                default:    return EMPTY;
            }
        }

    }

    public enum OBSTACLETYPE {
        EMPTY,
        Obstacle,
        Vehicle,
        passedObstacle
    }
    OBSTACLETYPE obstacleType;
    Direction direction;
    int x_coord;
    int y_coord;
    boolean occupied;
    boolean verified;
    int obstacle_number;

    ObstacleData(int x_coord, int y_coord, Direction direction, boolean occupied, OBSTACLETYPE obstacleType, boolean verified, int obstacle_number) {
        this.x_coord = x_coord;
        this.y_coord = y_coord;
        this.direction = direction;
        this.occupied = occupied;
        this.obstacleType = obstacleType;
        this.verified = verified;
        this.obstacle_number = obstacle_number;
    }

    public void setDirection(Direction direction){
        this.direction = direction;
    }
    public Direction getDirection(){
        return this.direction;
    }
    public void setxCoord(int x_coord){
        this.x_coord = x_coord;
    }
    public int getXCoord(){
        return this.x_coord;
    }

    public void setYCoord(int y_coord){
        this.y_coord = y_coord;
    }
    public int getYCoord(){
        return this.y_coord;
    }

    public void setObstacleType(OBSTACLETYPE obstacleType){
        this.obstacleType= obstacleType;
    }
    public OBSTACLETYPE getObstacleType(){
        return this.obstacleType;
    }

    public void setOccupied(boolean occupied){
        this.occupied = occupied;
    }
    public boolean getOccupied(){
        return this.occupied;
    }

    public void setVerified(boolean verified){
        this.verified = verified;
    }
    public boolean getVerified(){
        return this.verified;
    }

    public void setObstacleNumber(int obstacle_number){
        this.obstacle_number = obstacle_number;
    }
    public int getObstacleNumber(){
        return this.obstacle_number;
    }

    public void printObstacleData(){
        Log.d("ObstacleClass:", "X Coordinate "+utilities.convertIntToString(this.x_coord));
        Log.d("ObstacleClass:", "Y Coordinate "+utilities.convertIntToString(this.y_coord));
        Log.d("ObstacleClass:", "Occupied Status "+utilities.convertBooleanToString(this.occupied));
        Log.d("ObstacleClass:", "Direction Status "+this.direction.name());
        Log.d("ObstacleClass:", "Obstacle Name "+this.obstacleType.name());
        Log.d("ObstacleClass:", "Verified Status "+this.verified);
        Log.d("ObstacleClass:", "Obstacle Number "+this.obstacle_number);
    }



}

