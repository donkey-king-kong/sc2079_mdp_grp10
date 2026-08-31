class CommandRouter:
    def classify_algo_command(self, command: str):
        if command.startswith(("SF", "SB", "LF", "RF", "LB", "RB")):
            return {
                "target": "stm",
                "value": command,
            }

        if command.startswith("SNAP"):
            obstacle_id = command[4:]

            return {
                "target": "camera",
                "value": obstacle_id,
            }

        if command == "FIN":
            return {
                "target": "finish",
                "value": None,
            }

        return {
            "target": "unknown",
            "value": command,
        }