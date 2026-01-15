package com.example.keymanager;

import android.Manifest;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat; // For color handling

import java.util.UUID;

public class MainActivity extends AppCompatActivity {

    // --- UUID CONFIGURATION (Must match Arduino Sketch) ---
    private static final UUID SERVICE_UUID = UUID.fromString("0000AAA0-0000-1000-8000-00805f9b34fb");
    private static final UUID CHAR_STATUS_UUID = UUID.fromString("0000AAA1-0000-1000-8000-00805f9b34fb");
    private static final UUID CHAR_COMMAND_UUID = UUID.fromString("0000AAA2-0000-1000-8000-00805f9b34fb");
    private static final UUID CONFIG_DESCRIPTOR = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    // --- BLE VARIABLES ---
    private BluetoothAdapter bluetoothAdapter;
    private BluetoothLeScanner bluetoothLeScanner;
    private BluetoothGatt bluetoothGatt;
    private boolean isScanning = false;

    // --- UI ELEMENTS ---
    private TextView txtStatus, txtLog;
    private EditText inputUserId; // Renamed from inputID for clarity
    private Button btnConnect, btnEnroll, btnDelete;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // UI Binding
        txtStatus = findViewById(R.id.txtStatus);
        txtLog = findViewById(R.id.txtLog);
        inputUserId = findViewById(R.id.inputUserId); // Ensure XML ID matches
        btnConnect = findViewById(R.id.btnConnect);
        btnEnroll = findViewById(R.id.btnEnroll);
        btnDelete = findViewById(R.id.btnDelete);

        // Bluetooth Initialization
        BluetoothManager bluetoothManager = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        bluetoothAdapter = bluetoothManager.getAdapter();

        // Initial Permission Check
        checkPermissions();

        // --- BUTTON LISTENERS ---

        // 1. Connect / Scan
        btnConnect.setOnClickListener(v -> startScan());

        // 2. Enroll Fingerprint
        btnEnroll.setOnClickListener(v -> {
            String idStr = inputUserId.getText().toString();
            if(!idStr.isEmpty() && bluetoothGatt != null) {
                sendCommand("ENROLL:" + idStr);
                txtLog.setText("Requesting enrollment for ID: " + idStr);
            } else {
                Toast.makeText(this, "Not connected or ID empty", Toast.LENGTH_SHORT).show();
            }
        });

        // 3. Delete Fingerprint
        btnDelete.setOnClickListener(v -> {
            String idStr = inputUserId.getText().toString();
            if(!idStr.isEmpty() && bluetoothGatt != null) {
                sendCommand("DELETE:" + idStr);
                txtLog.setText("Sending delete command for ID: " + idStr);
            } else {
                Toast.makeText(this, "Not connected or ID empty", Toast.LENGTH_SHORT).show();
            }
        });
    }

    // --- BLE SCANNING LOGIC ---
    @SuppressLint("MissingPermission")
    private void startScan() {
        if (bluetoothAdapter == null || !bluetoothAdapter.isEnabled()) {
            Toast.makeText(this, "Please enable Bluetooth!", Toast.LENGTH_SHORT).show();
            return;
        }

        bluetoothLeScanner = bluetoothAdapter.getBluetoothLeScanner();
        if (!isScanning) {
            // Stop scan after 10 seconds
            new Handler(Looper.getMainLooper()).postDelayed(() -> {
                if (isScanning) {
                    isScanning = false;
                    if (checkPermissions() && bluetoothLeScanner != null) {
                        bluetoothLeScanner.stopScan(scanCallback);
                    }
                    updateStatus("Scan Timeout", false);
                    btnConnect.setEnabled(true);
                }
            }, 10000);

            isScanning = true;
            btnConnect.setEnabled(false); // Disable button while scanning
            if (checkPermissions()) {
                // IMPORTANT: Looks for the exact name defined in Arduino
                updateStatus("Scanning for 'Carberus_Key_Manager'...", false);
                bluetoothLeScanner.startScan(scanCallback);
            }
        }
    }

    // Scan Callback
    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        @SuppressLint("MissingPermission")
        public void onScanResult(int callbackType, ScanResult result) {
            BluetoothDevice device = result.getDevice();
            if (device.getName() != null) {
                // Match the name defined in Arduino Sketch: BLE.setLocalName("Carberus_Key_Manager");
                if (device.getName().equals("Carberus_Key_Manager")) {
                    updateStatus("Key Found! Connecting...", false);

                    // Stop scanning immediately
                    isScanning = false;
                    bluetoothLeScanner.stopScan(this);

                    // Connect to GATT
                    bluetoothGatt = device.connectGatt(MainActivity.this, false, gattCallback);
                }
            }
        }
    };

    // --- GATT CALLBACKS ---
    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override
        @SuppressLint("MissingPermission")
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                runOnUiThread(() -> {
                    txtStatus.setText("Status: CONNECTED");
                    txtStatus.setTextColor(ContextCompat.getColor(MainActivity.this, android.R.color.holo_green_dark));
                    btnConnect.setText("Connected");
                });
                gatt.discoverServices();

            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                runOnUiThread(() -> {
                    txtStatus.setText("Status: DISCONNECTED");
                    txtStatus.setTextColor(ContextCompat.getColor(MainActivity.this, android.R.color.holo_red_dark));
                    btnConnect.setText("Connect");
                    btnConnect.setEnabled(true);
                });
            }
        }

        @Override
        @SuppressLint("MissingPermission")
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                BluetoothGattService service = gatt.getService(SERVICE_UUID);
                if (service != null) {
                    // Enable Notifications for Status Characteristic
                    BluetoothGattCharacteristic statusChar = service.getCharacteristic(CHAR_STATUS_UUID);
                    if (statusChar != null) {
                        gatt.setCharacteristicNotification(statusChar, true);

                        BluetoothGattDescriptor descriptor = statusChar.getDescriptor(CONFIG_DESCRIPTOR);
                        if (descriptor != null) {
                            descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                            gatt.writeDescriptor(descriptor);
                        }
                    }
                }
            }
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic) {
            if (CHAR_STATUS_UUID.equals(characteristic.getUuid())) {
                // Incoming message from Arduino (e.g. "Place finger...")
                String msg = characteristic.getStringValue(0);

                runOnUiThread(() -> {
                    txtLog.append("\n" + msg); // Append to log instead of replacing
                    // Optional: Scroll logic here
                });
                Log.d("BLE", "Notification: " + msg);
            }
        }
    };

    // --- UTILITIES ---

    @SuppressLint("MissingPermission")
    private void sendCommand(String cmd) {
        if (bluetoothGatt == null) return;

        BluetoothGattService service = bluetoothGatt.getService(SERVICE_UUID);
        if (service != null) {
            BluetoothGattCharacteristic cmdChar = service.getCharacteristic(CHAR_COMMAND_UUID);
            if (cmdChar != null) {
                cmdChar.setValue(cmd);
                bluetoothGatt.writeCharacteristic(cmdChar);
                Log.d("BLE", "Command sent: " + cmd);
            }
        }
    }

    private void updateStatus(String msg, boolean success) {
        runOnUiThread(() -> txtStatus.setText(msg));
    }

    private boolean checkPermissions() {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, new String[]{
                    Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT,
                    Manifest.permission.ACCESS_FINE_LOCATION}, 1);
            return false;
        }
        return true;
    }
}