from protocol import AndroidStreamParser


def main():
    parser = AndroidStreamParser()

    print("TEST 1: Complete JSON")
    print(
        parser.feed(
            '{"cat":"stm","value":"<FW010>"}'
        )
    )

    print("\nTEST 2: Fragmented JSON")
    print(
        parser.feed(
            '{"cat":"stm","value":"<FR'
        )
    )
    print(
        parser.feed(
            '090>"}'
        )
    )

    print("\nTEST 3: Two JSON messages together")
    print(
        parser.feed(
            '{"cat":"stm","value":"<FW010>"}'
            '{"cat":"stm","value":"<BW010>"}'
        )
    )

    print("\nTEST 4: Plain command")
    print(
        parser.feed(
            "sendArena"
        )
    )

    print("\nTEST 5: Fragmented plain command")
    print(
        parser.feed(
            "begin"
        )
    )
    print(
        parser.feed(
            "Explore"
        )
    )

    print("\nTEST 6: Plain command followed by JSON")
    print(
        parser.feed(
            'sendArena'
            '{"cat":"stm","value":"<FL090>"}'
        )
    )

    print("\nTEST 7: Unknown junk followed by valid JSON")
    print(
        parser.feed(
            'xyz'
            '{"cat":"stm","value":"<BR090>"}'
        )
    )


if __name__ == "__main__":
    main()