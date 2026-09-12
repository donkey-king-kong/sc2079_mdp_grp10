from router import CommandRouter


def main():
    router = CommandRouter()

    commands = [
        "SF050",
        "RF117",
        "LF012",
        "SNAP2",
        "SB015",
        "SNAP1",
        "FIN",
        "HELLO",
    ]

    for command in commands:
        result = router.classify_algo_command(command)
        print(command, "->", result)


if __name__ == "__main__":
    main()