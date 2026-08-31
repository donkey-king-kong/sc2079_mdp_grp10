from protocol import AndroidStreamParser


def main():
    parser = AndroidStreamParser()

    chunks = [
        '{"cat": "stm", "value": "<FW010>"}{"cat": "stm",',
        ' "value": "<FR090>"}',
    ]

    for chunk in chunks:
        messages = parser.feed(chunk)

        for message in messages:
            print(message)


if __name__ == "__main__":
    main()