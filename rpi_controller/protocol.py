import json


class AndroidStreamParser:
    def __init__(self):
        self.buffer = ""
        self.decoder = json.JSONDecoder()

        self.plain_commands = {
            "sendArena",
            "beginExplore",
            "beginFastest",
            "tr",
            "tl",
        }

    def feed(self, chunk: str):
        self.buffer += chunk
        messages = []

        while self.buffer:
            self.buffer = self.buffer.lstrip()

            if not self.buffer:
                break

            # JSON message
            if self.buffer.startswith("{"):
                try:
                    message, index = self.decoder.raw_decode(self.buffer)

                    messages.append(message)
                    self.buffer = self.buffer[index:]
                    continue

                except json.JSONDecodeError:
                    # JSON may be incomplete, so wait for more bytes
                    break

            # Known plain-text Android command
            matched_command = None

            for command in self.plain_commands:
                if self.buffer.startswith(command):
                    matched_command = command
                    break

            if matched_command is not None:
                messages.append({
                    "cat": matched_command,
                    "value": None,
                })

                self.buffer = self.buffer[len(matched_command):]
                continue

            # Check whether current buffer could be the beginning
            # of a known plain command.
            possible_partial = any(
                command.startswith(self.buffer)
                for command in self.plain_commands
            )

            if possible_partial:
                break

            # Unknown data: remove one character so the parser
            # cannot get permanently stuck.
            self.buffer = self.buffer[1:]

        return messages