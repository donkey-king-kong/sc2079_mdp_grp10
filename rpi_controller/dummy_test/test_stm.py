from connectors.stm import STMConnector


class FakeSTMConnection:
    def __init__(self):
        self.is_open = True
        self.sent = []

    def write(self, data):
        self.sent.append(data)

    def flush(self):
        pass

    def readline(self):
        return b"A\n"

    def close(self):
        self.is_open = False


def main():
    stm = STMConnector()

    fake_connection = FakeSTMConnection()
    stm.connection = fake_connection

    print("Sending command...")
    stm.send_command("SF010")

    print("Bytes sent:")
    print(fake_connection.sent)

    print("\nReading ACK...")
    ack = stm.read_ack()
    print("ACK:", ack)

    print("\nDisconnecting...")
    stm.disconnect()


if __name__ == "__main__":
    main()