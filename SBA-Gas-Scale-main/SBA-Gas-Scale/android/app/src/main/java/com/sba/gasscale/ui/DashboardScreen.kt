package com.sba.gasscale.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sba.gasscale.ble.TelemetryData

@Composable
fun DashboardScreen(
    telemetry: TelemetryData,
    isScanning: Boolean,
    onConnectClick: () -> Unit,
    onDisconnectClick: () -> Unit
) {
    val fuelPercentage = ((telemetry.netWeight / telemetry.selectedMaxKg) * 100f).coerceIn(0f, 100f)
    val animatedFuelProgress by animateFloatAsState(
        targetValue = fuelPercentage / 100f,
        animationSpec = tween(durationMillis = 600),
        label = "FuelProgress"
    )

    val (gasColor, gasLabel) = when {
        telemetry.gasPercentage <= 20f -> Color(0xFF10B981) to "Safe"
        telemetry.gasPercentage <= 59f -> Color(0xFFF59E0B) to "Warning"
        else -> Color(0xFFEF4444) to "CRITICAL"
    }

    val animatedGasColor by animateColorAsState(targetValue = gasColor, label = "GasColor")

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF090D16))
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // BLE Connection Header Card
        Card(
            shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF161E31)),
            modifier = Modifier.fillMaxWidth()
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text(
                        text = if (telemetry.isConnected) "SBA GAS DETECTOR" else "Appliance Offline",
                        color = Color.White,
                        fontWeight = FontWeight.Bold,
                        fontSize = 18.sp
                    )
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                        modifier = Modifier.padding(top = 4.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(10.dp)
                                .clip(CircleShape)
                                .background(if (telemetry.isConnected) Color(0xFF10B981) else Color(0xFF6B7280))
                        )
                        Text(
                            text = if (telemetry.isConnected) "BLE Connected" else "Not Linked",
                            color = Color(0xFF9CA3AF),
                            fontSize = 12.sp
                        )
                    }
                }

                Button(
                    onClick = { if (telemetry.isConnected) onDisconnectClick() else onConnectClick() },
                    shape = RoundedCornerShape(12.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (telemetry.isConnected) Color(0xFF374151) else Color(0xFF00F2FE),
                        contentColor = if (telemetry.isConnected) Color.White else Color(0xFF090D16)
                    )
                ) {
                    Text(
                        text = when {
                            telemetry.isConnected -> "Disconnect"
                            isScanning -> "Scanning..."
                            else -> "Connect"
                        },
                        fontWeight = FontWeight.Bold
                    )
                }
            }
        }

        // Alarm Banner
        if (telemetry.alarmState == 2 || telemetry.gasPercentage > 60f) {
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = Color(0x38EF4444)),
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.dp, Color(0xFFEF4444), RoundedCornerShape(16.dp))
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text(
                        text = "🚨 CRITICAL GAS LEAK DETECTED!",
                        color = Color.White,
                        fontWeight = FontWeight.ExtraBold,
                        fontSize = 16.sp
                    )
                    Text(
                        text = "MQ-5 combustible gas threshold exceeded. GSM voice/SMS escalation triggered.",
                        color = Color(0xFFFCA5A5),
                        fontSize = 13.sp,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
            }
        } else if (telemetry.alarmState == 1 || telemetry.netWeight < 1.0f) {
            Card(
                shape = RoundedCornerShape(16.dp),
                colors = CardDefaults.cardColors(containerColor = Color(0x38F59E0B)),
                modifier = Modifier
                    .fillMaxWidth()
                    .border(1.dp, Color(0xFFF59E0B), RoundedCornerShape(16.dp))
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text(
                        text = "⚠️ LOW FUEL WARNING",
                        color = Color.White,
                        fontWeight = FontWeight.Bold,
                        fontSize = 16.sp
                    )
                    Text(
                        text = "Liquid gas net weight is under 1.0 kg remaining (${String.format("%.2f", telemetry.netWeight)} kg).",
                        color = Color(0xFFFDE68A),
                        fontSize = 13.sp,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
            }
        }

        // Circular Progress Fuel Gauge Card
        Card(
            shape = RoundedCornerShape(24.dp),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF161E31)),
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(20.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.SpaceAround
            ) {
                // Circular Ring UI
                Box(
                    contentAlignment = Alignment.Center,
                    modifier = Modifier.size(200.dp)
                ) {
                    androidx.compose.foundation.Canvas(modifier = Modifier.fillMaxSize()) {
                        val strokeWidth = 14.dp.toPx()
                        // Background circle
                        drawArc(
                            color = Color(0x1FFFFFFF),
                            startAngle = -90f,
                            sweepAngle = 360f,
                            useCenter = false,
                            style = Stroke(width = strokeWidth, cap = StrokeCap.Round)
                        )
                        // Active progress arc
                        drawArc(
                            brush = Brush.linearGradient(
                                colors = listOf(Color(0xFF00F2FE), Color(0xFF4FACFE))
                            ),
                            startAngle = -90f,
                            sweepAngle = animatedFuelProgress * 360f,
                            useCenter = false,
                            style = Stroke(width = strokeWidth, cap = StrokeCap.Round)
                        )
                    }

                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(
                            text = "${fuelPercentage.toInt()}%",
                            color = Color.White,
                            fontSize = 42.sp,
                            fontWeight = FontWeight.ExtraBold
                        )
                        Text(
                            text = "${String.format("%.2f", telemetry.netWeight)} kg",
                            color = Color(0xFF00F2FE),
                            fontSize = 18.sp,
                            fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.Bold
                        )
                        Text(
                            text = "FUEL REMAINING",
                            color = Color(0xFF9CA3AF),
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }

                // Grid stats
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    // MQ-5 Combustible Box
                    Card(
                        shape = RoundedCornerShape(16.dp),
                        colors = CardDefaults.cardColors(containerColor = Color(0x0FFFFFFF)),
                        modifier = Modifier.weight(1f)
                    ) {
                        Column(modifier = Modifier.padding(12.dp)) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text("MQ-5 Gas", color = Color(0xFF9CA3AF), fontSize = 11.sp)
                                Text(gasLabel, color = animatedGasColor, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                            }
                            Text(
                                text = "${String.format("%.1f", telemetry.gasPercentage)}%",
                                color = Color.White,
                                fontSize = 20.sp,
                                fontFamily = FontFamily.Monospace,
                                fontWeight = FontWeight.Bold,
                                modifier = Modifier.padding(vertical = 4.dp)
                            )
                        }
                    }

                    // Limit Box
                    Card(
                        shape = RoundedCornerShape(16.dp),
                        colors = CardDefaults.cardColors(containerColor = Color(0x0FFFFFFF)),
                        modifier = Modifier.weight(1f)
                    ) {
                        Column(modifier = Modifier.padding(12.dp)) {
                            Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text("Cylinder Limit", color = Color(0xFF9CA3AF), fontSize = 11.sp)
                                Text("${telemetry.selectedMaxKg.toInt()} kg", color = Color(0xFF00F2FE), fontSize = 11.sp, fontWeight = FontWeight.Bold)
                            }
                            Text(
                                text = "${String.format("%.2f", telemetry.netWeight)} kg",
                                color = Color.White,
                                fontSize = 20.sp,
                                fontFamily = FontFamily.Monospace,
                                fontWeight = FontWeight.Bold,
                                modifier = Modifier.padding(vertical = 4.dp)
                            )
                        }
                    }
                }
            }
        }
    }
}
