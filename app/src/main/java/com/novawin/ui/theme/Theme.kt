package com.novawin.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

// Paleta inspirada no Windows 11 (modo escuro)
val NwBackground = Color(0xFF0B0F1A)
val NwSurface = Color(0xFF151B2B)
val NwSurfaceAlt = Color(0xFF1B2338)
val NwCard = Color(0xFF1A2234)
val NwStroke = Color(0xFF2A3550)
val NwAccent = Color(0xFF3B82F6)
val NwAccentSoft = Color(0x333B82F6)
val NwTextPrimary = Color(0xFFE8ECF5)
val NwTextSecondary = Color(0xFF9AA5BD)
val NwOk = Color(0xFF34C759)
val NwError = Color(0xFFFF453A)

val NwShapes = Shapes(
    extraSmall = RoundedCornerShape(8.dp),
    small = RoundedCornerShape(12.dp),
    medium = RoundedCornerShape(16.dp),
    large = RoundedCornerShape(20.dp),
    extraLarge = RoundedCornerShape(28.dp)
)

private val NwColors = darkColorScheme(
    primary = NwAccent,
    onPrimary = Color.White,
    secondary = NwAccent,
    background = NwBackground,
    surface = NwSurface,
    surfaceVariant = NwCard,
    onBackground = NwTextPrimary,
    onSurface = NwTextPrimary,
    onSurfaceVariant = NwTextSecondary,
    outline = NwStroke,
    error = NwError
)

@Composable
fun NovaWinTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = NwColors,
        shapes = NwShapes,
        content = content
    )
}
