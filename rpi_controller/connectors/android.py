import json


class AndroidConnector:
    def parse_message(self, raw_message: str):
        raw_message = raw_message.strip()

        if not raw_message:
            return None

        # Plain-text command used by the Android app
        if raw_message == "sendArena":
            return {
                "cat": "sendArena",
                "value": None,
            }

        # JSON messages
        try:
            return json.loads(raw_message)

        except json.JSONDecodeError:
            return {
                "cat": "unknown",
                "value": raw_message,
            }

    def to_stm_command(self, android_command: str):
        if not android_command.startswith("<") or not android_command.endswith(">"):
            return None

        command = android_command[1:-1]

        if len(command) < 3:
            return None

        prefix = command[:2]
        value = command[2:]

        prefix_map = {
            "FW": "SF",
            "BW": "SB",
            "FL": "LF",
            "FR": "RF",
            "BL": "LB",
            "BR": "RB",
        }

        stm_prefix = prefix_map.get(prefix)

        if stm_prefix is None:
            return None

        return stm_prefix + value