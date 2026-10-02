package com.example.sc2079

import com.example.sc2079.service.BluetoothMessageBuffer
import com.google.gson.Gson
import com.google.gson.JsonObject
import org.junit.Assert.*
import org.junit.Test
import java.util.Base64

class ExampleUnitTest {
    private val gson = Gson()

    private fun stitchMessage(value: String): String =
        gson.toJson(mapOf("cat" to "stitch-image", "value" to value))

    @Test
    fun stitchedImageSurvivesFragmentedAndCombinedReads() {
        val imageBytes = ByteArray(10000) { (it % 256).toByte() }
        val encoded = Base64.getEncoder().encodeToString(imageBytes)
        val expected = listOf("TARGET,1,38", stitchMessage("starting stitch")) +
            encoded.chunked(700).map { stitchMessage(it) } + stitchMessage("ending stitch")
        val wire = (expected.joinToString("\n") + "\n").toByteArray()
        for (readSize in listOf(1, 13, 700, 1024, wire.size)) {
            val buffer = BluetoothMessageBuffer()
            val received = wire.asList().chunked(readSize).flatMap { buffer.feed(it.toByteArray()) }
            assertEquals("Read size $readSize", expected, received)
            val chunks = received.drop(2).dropLast(1).joinToString("") {
                gson.fromJson(it, JsonObject::class.java).get("value").asString
            }
            assertArrayEquals(imageBytes, Base64.getDecoder().decode(chunks))
        }
    }

    @Test
    fun incompleteMessageWaitsForNextRead() {
        val buffer = BluetoothMessageBuffer()
        val message = stitchMessage("starting stitch")
        assertTrue(buffer.feed(message.take(20).toByteArray()).isEmpty())
        assertEquals(listOf(message), buffer.feed(message.drop(20).toByteArray()))
        assertTrue(buffer.feed("\r\n".toByteArray()).isEmpty())
    }

    @Test
    fun combinedMessagesKeepTrailingPartialMessage() {
        val buffer = BluetoothMessageBuffer()
        val start = stitchMessage("starting stitch")
        val chunk = stitchMessage("YWJjZA==")
        assertEquals(listOf(start), buffer.feed((start + "\n" + chunk.take(10)).toByteArray()))
        assertEquals(listOf(chunk, "ROBOT,1,2,N"),
            buffer.feed((chunk.drop(10) + "\nROBOT,1,2,N\n").toByteArray()))
    }

    @Test
    fun legacyJsonAndUtf8SurviveWithoutNewlines() {
        val expected = listOf(gson.toJson(mapOf("value" to "brace } quote \" slash \\ café")),
            "{\"value\":{\"nested\":true}}")
        val buffer = BluetoothMessageBuffer()
        val received = expected.joinToString("").toByteArray().flatMap { buffer.feed(byteArrayOf(it)) }
        assertEquals(expected, received)
    }

    @Test
    fun onlyBytesReadFromSocketAreConsumed() {
        val buffer = BluetoothMessageBuffer()
        val message = stitchMessage("ending stitch")
        val socketBuffer = (message + "stale bytes from previous read").toByteArray()
        assertEquals(listOf(message), buffer.feed(socketBuffer, message.toByteArray().size))
    }

    @Test
    fun tenNamedPresetsRoundTripWithTheirOwnLayoutAndArenaSize() {
        var presets = emptyList<MapPreset>()
        repeat(10) { index ->
            presets = MapPresets.save(presets, MapPreset("Map $index", 10 + index, 20, "layout-$index"))
        }
        val restored = MapPresets.decode(gson.toJson(presets), null)
        assertEquals(presets, restored)
        assertEquals(10, restored.size)
        assertEquals("layout-4", restored[4].gridJson)
        assertEquals(14, restored[4].columns)
        assertThrows(IllegalArgumentException::class.java) {
            MapPresets.save(restored, MapPreset("Eleventh", 20, 20, "new-layout"))
        }
    }

    @Test
    fun sameNameCanReplacePresetEvenWhenFull() {
        val presets = (1..10).map { MapPreset("Map $it", 20, 20, "old-$it") }
        val updated = MapPresets.save(presets, MapPreset("  MAP 5  ", 15, 12, "replacement"))
        assertEquals(10, updated.size)
        assertEquals(MapPreset("MAP 5", 15, 12, "replacement"), updated[4])
        assertEquals(presets[0], updated[0])
        assertEquals("old-5", presets[4].gridJson)
    }

    @Test
    fun legacyMapIsPreservedAndDoesNotReturnAfterDeletingAllPresets() {
        val legacy = "[[{\"occupied\":true}]]"
        assertEquals(listOf(MapPreset("Saved Map", 20, 20, legacy)), MapPresets.decode(null, legacy))
        assertTrue(MapPresets.decode("[]", legacy).isEmpty())
        assertTrue(MapPresets.decode(null, null).isEmpty())
    }

    @Test
    fun presetNamesAreValidatedAndDeletedSlotCanBeReused() {
        assertThrows(IllegalArgumentException::class.java) {
            MapPresets.save(emptyList(), MapPreset("   ", 20, 20, "grid"))
        }
        val presets = (1..10).map { MapPreset("Map $it", 20, 20, "grid-$it") }
        val afterDeletion = presets.filterNot { it.name == "Map 3" }
        val updated = MapPresets.save(afterDeletion, MapPreset("New map", 20, 20, "new"))
        assertEquals(10, updated.size)
        assertFalse(updated.any { it.name == "Map 3" })
        assertEquals("New map", updated.last().name)
    }

    private fun savedGrid(rowCount: Int): ArrayList<ArrayList<ObstacleData>> =
        ArrayList((0 until rowCount).map {
            ArrayList((0 until 20).map {
                ObstacleData(-1, -1, ObstacleData.Direction.EMPTY, false,
                    ObstacleData.OBSTACLETYPE.EMPTY, false, 0)
            })
        })

    @Test
    fun oldFortyRowPresetRestoresVisibleObstaclesWithoutLosingDirectionOrIds() {
        val grid = savedGrid(40)
        grid[7][4] = ObstacleData(4, 7, ObstacleData.Direction.WEST, true,
            ObstacleData.OBSTACLETYPE.Obstacle, false, 3)
        val preset = MapPreset("Existing map", 20, 20, gson.toJson(grid))
        val restored = MapPresets.restoreGrid(preset)
        assertEquals(20, restored.size)
        assertTrue(restored.all { it.size == 20 })
        assertEquals(ObstacleData.Direction.WEST, restored[7][4].direction)
        assertEquals(3, restored[7][4].obstacleNumber)
        assertTrue(restored[7][4].occupied)
        assertEquals(4, restored[7][4].xCoord)
        assertEquals(7, restored[7][4].yCoord)
        // The original saved preset is kept intact.
        assertEquals(40, gson.fromJson(preset.gridJson, com.google.gson.JsonArray::class.java).size())
    }

    @Test
    fun twentyRowPresetAndMigratedLegacySaveBothRestore() {
        val grid = savedGrid(20)
        val json = gson.toJson(grid)
        assertEquals(20, MapPresets.restoreGrid(MapPreset("New", 10, 15, json)).size)
        val legacy = MapPresets.decode(null, gson.toJson(savedGrid(40))).single()
        assertEquals(20, MapPresets.restoreGrid(legacy).size)
    }

    @Test
    fun unsupportedGridIsRejectedInsteadOfSilentlyDiscardingObstacles() {
        val grid = savedGrid(40)
        grid[25][2] = ObstacleData(2, 25, ObstacleData.Direction.NORTH, true,
            ObstacleData.OBSTACLETYPE.Obstacle, false, 1)
        assertThrows(IllegalArgumentException::class.java) {
            MapPresets.restoreGrid(MapPreset("Invalid", 20, 20, gson.toJson(grid)))
        }
        assertThrows(IllegalArgumentException::class.java) {
            MapPresets.restoreGrid(MapPreset("Incomplete", 20, 20, gson.toJson(savedGrid(19))))
        }
    }

    @Test
    fun arenaSnapshotReflectsMovedRemovedAndLoadedObstacles() {
        val grid = savedGrid(20)
        grid[2][3] = ObstacleData(3, 2, ObstacleData.Direction.EAST, true,
            ObstacleData.OBSTACLETYPE.Obstacle, false, 1)
        grid[4][5] = ObstacleData(5, 4, ObstacleData.Direction.WEST, true,
            ObstacleData.OBSTACLETYPE.Obstacle, false, 2)
        fun snapshot() = gson.fromJson(utilities.arenaMessage("arena-update", grid, 20, 20), JsonObject::class.java)
        assertEquals("arena-update", snapshot()["cat"].asString)
        assertEquals(2, snapshot()["value"].asJsonObject["obstacles"].asJsonArray.size())
        grid[6][7] = grid[2][3]
        grid[2][3] = savedGrid(1)[0][0]
        val moved = snapshot()["value"].asJsonObject["obstacles"].asJsonArray
            .map { it.asJsonObject }.first { it["id"].asInt == 1 }
        assertEquals(7, moved["x"].asInt)
        assertEquals(6, moved["y"].asInt)
        assertEquals(1, moved["d"].asInt) // RPi expects east=1, not Android's enum code 2.
        grid[4][5] = savedGrid(1)[0][0]
        assertEquals(1, snapshot()["value"].asJsonObject["obstacles"].asJsonArray.size())
        val restored = MapPresets.restoreGrid(MapPreset("Load", 20, 20, gson.toJson(grid)))
        assertEquals(snapshot(), gson.fromJson(utilities.arenaMessage("arena-update", restored, 20, 20), JsonObject::class.java))
        val start = gson.fromJson(utilities.arenaMessage("sendArena", grid, 20, 20), JsonObject::class.java)
        assertEquals("sendArena", start["cat"].asString)
        assertEquals(snapshot()["value"], start["value"])
    }

    @Test
    fun arenaSnapshotUsesRpiDirectionCodesAndCanClearAllObstacles() {
        val grid = savedGrid(20)
        val directions = listOf(ObstacleData.Direction.NORTH, ObstacleData.Direction.EAST,
            ObstacleData.Direction.SOUTH, ObstacleData.Direction.WEST)
        directions.forEachIndexed { index, direction ->
            grid[1][index] = ObstacleData(index, 1, direction, true, ObstacleData.OBSTACLETYPE.Obstacle, false, index + 1)
        }
        grid[5][5] = ObstacleData(5, 5, ObstacleData.Direction.WEST, true, ObstacleData.OBSTACLETYPE.Vehicle, false, -1)
        val arena = gson.fromJson(utilities.arenaMessage("arena-update", grid, 20, 20), JsonObject::class.java)["value"].asJsonObject
        assertEquals(listOf(0, 1, 2, 3), arena["obstacles"].asJsonArray.map { it.asJsonObject["d"].asInt })
        assertEquals(3, arena["robot_direction"].asInt)
        val empty = gson.fromJson(utilities.arenaMessage("arena-update", savedGrid(20), 20, 20), JsonObject::class.java)
        assertEquals(0, empty["value"].asJsonObject["obstacles"].asJsonArray.size())
    }
}
