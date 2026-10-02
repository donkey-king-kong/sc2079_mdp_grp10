package com.example.sc2079;

import static org.junit.Assert.assertEquals;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import org.junit.Test;

import java.util.ArrayList;

public class GridMapClassTest {
    @Test
    public void buildArenaDataPayload_serializesObstacleDirectionsForRpi() {
        ArrayList<ObstacleData> obstacles = new ArrayList<>();
        obstacles.add(obstacle(1, 2, ObstacleData.Direction.NORTH, 1));
        obstacles.add(obstacle(3, 4, ObstacleData.Direction.EAST, 2));
        obstacles.add(obstacle(5, 6, ObstacleData.Direction.SOUTH, 3));
        obstacles.add(obstacle(7, 8, ObstacleData.Direction.WEST, 4));

        JsonObject payload = JsonParser.parseString(
                GridMapClass.buildArenaDataPayload(obstacles, null)
        ).getAsJsonObject();

        JsonArray serializedObstacles = payload.getAsJsonArray("obstacles");
        assertEquals(4, serializedObstacles.size());
        assertEquals(1, serializedObstacles.get(0).getAsJsonObject().get("d").getAsInt());
        assertEquals(2, serializedObstacles.get(1).getAsJsonObject().get("d").getAsInt());
        assertEquals(3, serializedObstacles.get(2).getAsJsonObject().get("d").getAsInt());
        assertEquals(4, serializedObstacles.get(3).getAsJsonObject().get("d").getAsInt());
    }

    @Test
    public void buildArenaDataPayload_serializesRobotDirection() {
        assertRobotDirection(ObstacleData.Direction.NORTH, 1);
        assertRobotDirection(ObstacleData.Direction.EAST, 2);
        assertRobotDirection(ObstacleData.Direction.SOUTH, 3);
        assertRobotDirection(ObstacleData.Direction.WEST, 4);
    }

    private static ObstacleData obstacle(
            int x,
            int y,
            ObstacleData.Direction direction,
            int obstacleNumber
    ) {
        return new ObstacleData(
                x,
                y,
                direction,
                true,
                ObstacleData.OBSTACLETYPE.Obstacle,
                false,
                obstacleNumber
        );
    }

    private static void assertRobotDirection(ObstacleData.Direction direction, int expectedDirectionCode) {
        ArrayList<ObstacleData> obstacles = new ArrayList<>();
        ObstacleData bottomLeftRobot = new ObstacleData(
                10,
                11,
                direction,
                true,
                ObstacleData.OBSTACLETYPE.Vehicle,
                false,
                -1
        );

        JsonObject payload = JsonParser.parseString(
                GridMapClass.buildArenaDataPayload(obstacles, bottomLeftRobot)
        ).getAsJsonObject();

        assertEquals(10, payload.get("robot_x").getAsInt());
        assertEquals(11, payload.get("robot_y").getAsInt());
        assertEquals(expectedDirectionCode, payload.get("robot_direction").getAsInt());
    }
}
