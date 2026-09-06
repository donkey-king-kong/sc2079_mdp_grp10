from connectors.android import AndroidConnector


def main():
    android = AndroidConnector()

    test_messages = [
        '{"cat": "stm", "value": "<FW010>"}',
        '{"cat": "sendArena", "value": {"robot": {"x": 1, "y": 1}}}',
        "sendArena",
        "hello",
    ]

    for message in test_messages:
        result = android.parse_message(message)
        print(message, "->", result)

        if result and result.get("cat") == "stm":
            stm_command = android.to_stm_command(result["value"])
            print("Translated STM command:", stm_command)


if __name__ == "__main__":
    main()