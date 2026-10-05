package com.sba.gasscale.ble

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.ParcelUuid
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.*

data class TelemetryData(
    val gasPercentage: Float = 0.0f,
    val netWeight: Float = 0.0f,
    val alarmState: Int = 0,
    val isConnected: Boolean = false,
    val selectedMaxKg: Float = 6.0f,
    val phoneNum: String = "+2348012345678"
)

@SuppressLint("MissingPermission")
class BleManager(private val context: Context) {

    companion object {
        private const val TAG = "BleManager"
        val SERVICE_UUID: UUID = UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b")
        val CHARACTERISTIC_UUID: UUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8")
        val CLIENT_CHARACTERISTIC_CONFIG_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    private val bluetoothManager: BluetoothManager =
        context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager.adapter

    private var bluetoothGatt: BluetoothGatt? = null
    private var targetCharacteristic: BluetoothGattCharacteristic? = null

    private val _telemetry = MutableStateFlow(TelemetryData())
    val telemetry: StateFlow<TelemetryData> = _telemetry.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult?) {
            result?.device?.let { device ->
                Log.d(TAG, "Found target device: ${device.name} [${device.address}]")
                stopScan()
                connectToDevice(device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(TAG, "BLE Scan failed with code: $errorCode")
            _isScanning.value = false
        }
    }

    fun startScan() {
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled) {
            Log.e(TAG, "Bluetooth disabled or unavailable")
            return
        }
        val scanner = bluetoothAdapter.bluetoothLeScanner ?: return

        val filter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(SERVICE_UUID))
            .build()

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        _isScanning.value = true
        scanner.startScan(listOf(filter), settings, scanCallback)
        Log.d(TAG, "Started BLE scan for Service UUID: $SERVICE_UUID")
    }

    fun stopScan() {
        if (_isScanning.value) {
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
            _isScanning.value = false
        }
    }

    fun connectToDevice(device: BluetoothDevice) {
        Log.d(TAG, "Connecting to GATT on ${device.address}")
        bluetoothGatt = device.connectGatt(context, false, gattCallback)
    }

    fun disconnect() {
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        _telemetry.value = _telemetry.value.copy(isConnected = false)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt?, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.d(TAG, "GATT Connected. Discovering services...")
                _telemetry.value = _telemetry.value.copy(isConnected = true)
                gatt?.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.d(TAG, "GATT Disconnected.")
                _telemetry.value = _telemetry.value.copy(isConnected = false)
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt?, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt?.getService(SERVICE_UUID)
                targetCharacteristic = service?.getCharacteristic(CHARACTERISTIC_UUID)

                targetCharacteristic?.let { chara ->
                    gatt.setCharacteristicNotification(chara, true)
                    val descriptor = chara.getDescriptor(CLIENT_CHARACTERISTIC_CONFIG_UUID)
                    descriptor?.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    gatt.writeDescriptor(descriptor)
                    Log.d(TAG, "Subscribed to BLE Notifications for Characteristic $CHARACTERISTIC_UUID")
                }
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            val payload = String(value)
            parsePayload(payload)
        }

        @Deprecated("Deprecated in Java API 33")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt?,
            characteristic: BluetoothGattCharacteristic?
        ) {
            characteristic?.value?.let { bytes ->
                parsePayload(String(bytes))
            }
        }
    }

    private fun parsePayload(rawPayload: String) {
        // Format: DATA:<GasPercentage>,<NetWeight>,<AlarmState>
        if (rawPayload.startsWith("DATA:")) {
            val body = rawPayload.substring(5)
            val parts = body.split(",")
            if (parts.size >= 3) {
                try {
                    val gasPct = parts[0].toFloat()
                    val weightKg = parts[1].toFloat()
                    val alarmState = parts[2].toInt()

                    _telemetry.value = _telemetry.value.copy(
                        gasPercentage = gasPct,
                        netWeight = weightKg,
                        alarmState = alarmState
                    )
                } catch (e: Exception) {
                    Log.e(TAG, "Error parsing telemetry string: $rawPayload", e)
                }
            }
        }
    }

    fun sendCommand(cmd: String) {
        val chara = targetCharacteristic ?: return
        val gatt = bluetoothGatt ?: return

        chara.value = cmd.toByteArray()
        chara.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        gatt.writeCharacteristic(chara)
        Log.d(TAG, "Sent BLE Write Command: $cmd")
    }

    fun setMaxCapacity(maxKg: Float) {
        _telemetry.value = _telemetry.value.copy(selectedMaxKg = maxKg)
        sendCommand("MAX:${maxKg.toInt()}")
    }

    fun setPhoneNumber(phone: String) {
        _telemetry.value = _telemetry.value.copy(phoneNum = phone)
        sendCommand("NUM:$phone")
    }

    fun calibrateTare() {
        sendCommand("TARE")
    }
}
