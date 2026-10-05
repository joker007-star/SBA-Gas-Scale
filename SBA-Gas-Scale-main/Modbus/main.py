from pymodbus.client import ModbusSerialClient

# Define the port as a variable to avoid the attribute error
COM_PORT = 'COM8'

# Configure the client with your exact hardware settings
client = ModbusSerialClient(
    port=COM_PORT,
    baudrate=115200,   # 115.2 kbps
    parity='E',        # Even parity
    stopbits=1,        # 1 stop bit
    bytesize=7         # 7 data bits (Change to 8 if you get an RTU timeout)
)

print(f"Attempting to open {COM_PORT}...")

if client.connect():
    print(f"SUCCESS: {COM_PORT} opened.")
    print("Sending Modbus request to PLC...")
    
    # Attempt to read 1 holding register at address 0 from Slave ID 1
    result = client.read_holding_registers(address=0, count=1, slave=1)
    
    if not result.isError():
        print(f"SUCCESS! Data received from PLC: {result.registers}")
    else:
        print(f"FAILED: Port opened, but PLC did not respond.")
        print(f"Error details: {result}")
        print("Tip: If you are using Modbus RTU, try changing bytesize to 8.")
        
    client.close()
    print("Connection closed.")
else:
    print(f"FAILED: Could not open {COM_PORT}.")
    print("Ensure the cable is plugged in and no other software is using this COM port.")