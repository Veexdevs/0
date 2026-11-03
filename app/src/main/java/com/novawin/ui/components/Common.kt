package com.novawin.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.novawin.ui.theme.*

/** Logo NovaWin desenhado em Compose (janela + N). */
@Composable
fun NwLogo(size: Dp = 72.dp) {
    Canvas(Modifier.size(size)) {
        val w = size
        val corner = size * 0.22f
        val r = Rect(0f, 0f, w, w)
        drawRoundRect(color = NwSurface, topLeft = Offset(0f, 0f), size = Size(w, w), cornerRadius = CornerRadius(corner))
        drawRoundRect(
            color = NwAccent,
            topLeft = Offset(0f, 0f),
            size = Size(w, w * 0.34f),
            cornerRadius = CornerRadius(corner, corner)
        )
        // "N" (travessa desenhada com círculos sobrepostos)
        val cx = w / 2f
        val top = w * 0.42f
        val bottom = w * 0.8f
        val bw = w * 0.14f
        drawRect(NwTextPrimary, Offset(cx - w * 0.3f, top), Size(bw, bottom - top))
        drawRect(NwTextPrimary, Offset(cx + w * 0.3f - bw, top), Size(bw, bottom - top))
        for (i in 0..10) {
            val t = i / 10f
            val sx = cx - w * 0.3f + bw / 2f + (w * 0.3f - bw) * t
            val sy = top + (bottom - top) * t
            drawCircle(NwTextPrimary, radius = bw / 2f, center = Offset(sx, sy))
        }
    }
}

@Composable
fun TopBar(
    title: String,
    onBack: (() -> Unit)?,
    actions: @Composable RowScope.() -> Unit = {}
) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (onBack != null) {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Voltar", tint = NwTextPrimary)
            }
        }
        Spacer(Modifier.weight(1f))
        Text(
            text = title,
            color = NwTextPrimary,
            fontSize = 18.sp,
            fontWeight = FontWeight.SemiBold,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 8.dp)
        )
        Spacer(Modifier.width(36.dp))
        Row(verticalAlignment = Alignment.CenterVertically) { actions() }
    }
}

@Composable
fun NwCard(
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
    content: @Composable BoxScope.() -> Unit
) {
    val m = modifier
        .fillMaxWidth()
        .clip(RoundedCornerShape(16.dp))
        .background(NwCard)
        .border(1.dp, NwStroke, RoundedCornerShape(16.dp))
        .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
        .padding(16.dp)
    Box(m) { content() }
}

@Composable
fun NwActionButton(
    title: String,
    subtitle: String? = null,
    icon: ImageVector,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(if (enabled) NwCard else NwSurface)
            .border(1.dp, if (enabled) NwStroke else Color.Transparent, RoundedCornerShape(16.dp))
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(44.dp)
                .clip(RoundedCornerShape(12.dp))
                .background(NwAccentSoft),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, contentDescription = null, tint = NwAccent, modifier = Modifier.size(24.dp))
        }
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) {
            Text(title, color = NwTextPrimary, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
            if (subtitle != null) {
                Text(subtitle, color = NwTextSecondary, fontSize = 12.sp)
            }
        }
    }
}

@Composable
fun NwButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    accent: Boolean = true
) {
    Button(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.height(48.dp),
        shape = RoundedCornerShape(14.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = if (accent) NwAccent else NwSurfaceAlt,
            contentColor = if (accent) Color.White else NwTextPrimary
        )
    ) {
        Text(text, fontSize = 15.sp)
    }
}

@Composable
fun NwDialog(
    title: String,
    onDismiss: () -> Unit,
    actions: @Composable RowScope.() -> Unit,
    content: @Composable ColumnScope.() -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title, color = NwTextPrimary, fontSize = 18.sp) },
        text = {
            Column(Modifier.padding(top = 4.dp)) { content() }
        },
        confirmButton = {},
        dismissButton = {
            Row { actions() }
        },
        containerColor = NwSurface,
        shape = RoundedCornerShape(20.dp)
    )
}

@Composable
fun NwTextField(
    value: String,
    onValueChange: (String) -> Unit,
    label: String,
    modifier: Modifier = Modifier,
    keyboardType: androidx.compose.ui.text.input.KeyboardType? = null
) {
    OutlinedTextField(
        value = value,
        onValueChange = onValueChange,
        label = { Text(label, color = NwTextSecondary) },
        singleLine = true,
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp),
        shape = RoundedCornerShape(14.dp),
        colors = OutlinedTextFieldDefaults.colors(
            focusedTextColor = NwTextPrimary,
            unfocusedTextColor = NwTextPrimary,
            focusedBorderColor = NwAccent,
            unfocusedBorderColor = NwStroke,
            focusedLabelColor = NwAccent,
            cursorColor = NwAccent
        ),
        keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
            keyboardType = keyboardType ?: androidx.compose.ui.text.input.KeyboardType.Text
        )
    )
}

@Composable
fun SectionLabel(text: String, modifier: Modifier = Modifier) {
    Text(
        text = text,
        color = NwAccent,
        fontSize = 13.sp,
        fontWeight = FontWeight.SemiBold,
        modifier = modifier.padding(vertical = 10.dp)
    )
}

@Composable
fun SettingRow(
    title: String,
    subtitle: String?,
    onClick: () -> Unit = {}
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(NwCard)
            .border(1.dp, NwStroke, RoundedCornerShape(14.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 13.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, color = NwTextPrimary, fontSize = 14.5.sp, fontWeight = FontWeight.Medium)
            if (subtitle != null) {
                Text(subtitle, color = NwTextSecondary, fontSize = 12.5.sp)
            }
        }
        Icon(
            Icons.AutoMirrored.Filled.KeyboardArrowRight,
            contentDescription = null,
            tint = NwTextSecondary,
            modifier = Modifier.size(18.dp)
        )
    }
}
