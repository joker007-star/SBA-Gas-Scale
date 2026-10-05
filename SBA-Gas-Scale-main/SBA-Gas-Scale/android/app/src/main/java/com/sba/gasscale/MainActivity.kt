package com.sba.gasscale

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.core.content.ContextCompat
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.sba.gasscale.ble.BleManager
import com.sba.gasscale.ui.DashboardScreen
import com.sba.gasscale.ui.SettingsScreen

class MainActivity : ComponentActivity() {

    private lateinit var bleManager: BleManager

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val allGranted = permissions.entries.all { it.value }
        if (allGranted) {
            bleManager.startScan()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        bleManager = BleManager(this)

        setContent {
            MaterialTheme(
                colorScheme = darkColorScheme(
                    background = Color(0xFF090D16),
                    surface = Color(0xFF161E31)
                )
            ) {
                MainNavigation(
                    bleManager = bleManager,
                    onRequestPermissions = { requestBlePermissions() }
                )
            }
        }
    }

    private fun requestBlePermissions() {
        val permissions = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            permissions.add(Manifest.permission.BLUETOOTH_SCAN)
            permissions.add(Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            permissions.add(Manifest.permission.BLUETOOTH)
            permissions.add(Manifest.permission.BLUETOOTH_ADMIN)
            permissions.add(Manifest.permission.ACCESS_FINE_LOCATION)
        }

        val missing = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }

        if (missing.isEmpty()) {
            bleManager.startScan()
        } else {
            permissionLauncher.launch(missing.toTypedArray())
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        bleManager.disconnect()
    }
}

@Composable
fun MainNavigation(
    bleManager: BleManager,
    onRequestPermissions: () -> Unit
) {
    val navController = rememberNavController()
    val telemetry by bleManager.telemetry.collectAsState()
    val isScanning by bleManager.isScanning.collectAsState()

    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = navBackStackEntry?.destination?.route ?: "dashboard"

    Scaffold(
        bottomBar = {
            NavigationBar(
                containerColor = Color(0xFF121829),
                contentColor = Color.White
            ) {
                NavigationBarItem(
                    selected = currentRoute == "dashboard",
                    onClick = { navController.navigate("dashboard") },
                    label = { Text("Dashboard") },
                    icon = {}
                )
                NavigationBarItem(
                    selected = currentRoute == "settings",
                    onClick = { navController.navigate("settings") },
                    label = { Text("Hardware Setup") },
                    icon = {}
                )
            }
        }
    ) { paddingValues ->
        NavHost(
            navController = navController,
            startDestination = "dashboard",
            modifier = Modifier.padding(paddingValues)
        ) {
            composable("dashboard") {
                DashboardScreen(
                    telemetry = telemetry,
                    isScanning = isScanning,
                    onConnectClick = { onRequestPermissions() },
                    onDisconnectClick = { bleManager.disconnect() }
                )
            }
            composable("settings") {
                SettingsScreen(
                    telemetry = telemetry,
                    onSetMaxCapacity = { bleManager.setMaxCapacity(it) },
                    onSetPhoneNumber = { bleManager.setPhoneNumber(it) },
                    onCalibrateTare = { bleManager.calibrateTare() }
                )
            }
        }
    }
}
