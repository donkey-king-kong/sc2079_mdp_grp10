import sys

sys.path.append("..")

from connectors.stm import STMConnector


stm = STMConnector(
    port="/dev/ttyACM0",
    baudrate=115200,
    timeout=2,
)

try:
    stm.connect()

    while True:
        cmd_buffer = input("Command: ")
        command = cmd_buffer

        if command == "exit":
            break

        print(f"Sending: {command}")
        stm.send_command(command)

        ack = stm.read_ack()
        print(f"ACK: {ack}")

        if ack == "A":
            print("PASS")
        else:
            print("FAIL")

finally:
    stm.disconnect()
