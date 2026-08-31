from connectors.algo import AlgoConnector


def main():
    algo = AlgoConnector(
        host="127.0.0.1",
        port=5001,
    )

    print("Checking Algo server...")

    if not algo.check_health():
        print("Algo server is not reachable.")
        return

    print("Algo server is healthy.")

    robot = {
        "x": 1,
        "y": 1,
        "dir": "N",
    }

    obstacles = [
        {
            "id": 1,
            "x": 8,
            "y": 5,
            "dir": "S",
        },
        {
            "id": 2,
            "x": 14,
            "y": 6,
            "dir": "W",
        },
    ]

    print("\nRequesting navigation plan...")

    result = algo.navigate(
        robot=robot,
        obstacles=obstacles,
    )

    print("\nCommands:")

    for command in result["data"]["commands"]:
        print(command)


if __name__ == "__main__":
    main()