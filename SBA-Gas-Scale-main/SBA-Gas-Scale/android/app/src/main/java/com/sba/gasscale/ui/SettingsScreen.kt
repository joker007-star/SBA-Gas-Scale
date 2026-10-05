package com.sba.gasscale.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sba.gasscale.ble.TelemetryData

@Composable
fun SettingsScreen(
    telemetry: TelemetryData,
    onSetMaxCapacity: (Float) -> Unit,
    onSetPhoneNumber: (String) -> Unit,
    onCalibrateTare: () -> Unit
) {
    var phoneInput by remember { mutableStateOf(telemetry.phoneNum) }
    var showTareModal by remember { mutableStateOf(false) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF090D16))
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Text(
            text = "Hardware Setup",
            color = Color.White,
            fontSize = 22.sp,
            fontWeight = FontWeight.ExtraBold
        )

        // 1. Cylinder Capacity Selection
        Card(
            shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF161E31)),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Cylinder Capacity Selection",
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    fontSize = 16.sp
                )
                Text(
                    text = "Select your LPG cylinder size to calculate percentage remaining.",
                    color = Color(0xFF9CA3AF),
                    fontSize = 12.sp,
                    modifier = Modifier.padding(top = 2.dp, bottom = 12.dp)
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    listOf(3f, 5f, 6f).forEach { maxKg ->
                        val isSelected = telemetry.selectedMaxKg == maxKg
                        Button(
                            onClick = { onSetMaxCapacity(maxKg) },
                            modifier = Modifier.weight(1f),
                            shape = RoundedCornerShape(12.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = if (isSelected) Color(0x3300F2FE) else Color(0x1FFFFFFF),
                                contentColor = if (isSelected) Color(0xFF00F2FE) else Color.White
                            )
                        ) {
                            Text("${maxKg.toInt()} kg", fontWeight = FontWeight.Bold)
                        }
                    }
                }
            }
        }

        // 2. Emergency Contact Phone Number
        Card(
            shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF161E31)),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Emergency Alert Phone Number",
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    fontSize = 16.sp
                )
                Text(
                    text = "SIM800L module contacts this number for SMS and voice alerts.",
                    color = Color(0xFF9CA3AF),
                    fontSize = 12.sp,
                    modifier = Modifier.padding(top = 2.dp, bottom = 12.dp)
                )

                OutlinedTextField(
                    value = phoneInput,
                    onValueChange = { phoneInput = it },
                    label = { Text("Phone Number (+234...)") },
                    singleLine = true,
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = Color(0xFF00F2FE),
                        unfocusedBorderColor = Color(0xFF374151),
                        focusedTextColor = Color.White,
                        unfocusedTextColor = Color.White
                    ),
                    modifier = Modifier.fillMaxWidth()
                )

                Button(
                    onClick = { onSetPhoneNumber(phoneInput) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 12.dp),
                    shape = RoundedCornerShape(12.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF374151))
                ) {
                    Text("Save to Device (NUM:)", fontWeight = FontWeight.Bold)
                }
            }
        }

        // 3. Tare Calibration
        Card(
            shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF161E31)),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Live Tare Calibration",
                    color = Color.White,
                    fontWeight = FontWeight.Bold,
                    fontSize = 16.sp
                )
                Text(
                    text = "Zero the load cell weight after placing an empty cylinder onto the scale.",
                    color = Color(0xFF9CA3AF),
                    fontSize = 12.sp,
                    modifier = Modifier.padding(top = 2.dp, bottom = 12.dp)
                )

                Button(
                    onClick = { showTareModal = true },
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(12.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFEF4444))
                ) {
                    Text("Calibrate Empty Cylinder (TARE)", fontWeight = FontWeight.Bold)
                }
            }
        }
    }

    // Modal Confirmation Dialog for Tare
    if (showTareModal) {
        AlertDialog(
            onDismissRequest = { showTareModal = false },
            title = { Text("⚠️ Confirm Tare Calibration", fontWeight = FontWeight.Bold) },
            text = {
                Text("Ensure ONLY a completely EMPTY cylinder is resting on the scale before proceeding.\n\nThis will lock in the current steel tare weight into the ESP32 NVS memory.")
            },
            confirmButton = {
                Button(
                    onClick = {
                        showTareModal = false
                        onCalibrateTare()
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFFEF4444))
                ) {
                    Text("Execute Tare")
                }
            },
            dismissButton = {
                TextButton(onClick = { showTareModal = false }) {
                    Text("Cancel")
                }
            },
            containerColor = Color(0xFF121829),
            titleContentColor = Color.White,
            textContentColor = Color(0xFF9CA3AF)
        )
    }
}
