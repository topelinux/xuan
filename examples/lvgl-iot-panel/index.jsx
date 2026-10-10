import React, { useState } from 'react';
import { View, Text, Button, Switch, Slider, Render, Dimensions } from 'lvgljs-ui';

const { width, height } = Dimensions.window;

function Metric({ label, value, unit }) {
    return (
        <View style={styles.metric}>
            <Text style={styles.metricLabel}>{label}</Text>
            <Text style={styles.metricValue}>{value}{unit}</Text>
        </View>
    );
}

function App() {
    const [enabled, setEnabled] = useState(true);
    const [level, setLevel] = useState(42);

    return (
        <View style={styles.screen}>
            <View style={styles.header}>
                <Text style={styles.title}>IoT Panel</Text>
                <Text style={styles.subtitle}>xuan + React + LVGL</Text>
            </View>

            <View style={styles.metrics}>
                <Metric label="Temp" value="24.6" unit="C" />
                <Metric label="Humidity" value="51" unit="%" />
                <Metric label="Signal" value="-62" unit="dBm" />
            </View>

            <View style={styles.controls}>
                <Text style={styles.sectionTitle}>Output</Text>
                <Switch checked={enabled} onChange={() => setEnabled(!enabled)} />
                <Slider
                    style={styles.slider}
                    range={[0, 100]}
                    value={level}
                    onChange={(event) => setLevel(event?.value ?? level)}
                />
                <Button
                    style={styles.action}
                    onClick={() => setLevel(enabled ? 0 : 80)}
                >
                    <Text style={styles.actionText}>{enabled ? 'Stop' : 'Start'}</Text>
                </Button>
            </View>

            <View style={styles.footer}>
                <Text style={styles.footerText}>Level {level}%</Text>
            </View>
        </View>
    );
}

const styles = {
    screen: {
        width,
        height,
        display: 'flex',
        'flex-flow': 1,
        padding: 16,
        'row-spacing': 12,
        'background-color': 0x111827,
        'border-radius': 0,
        'border-width': 0,
    },
    header: {
        display: 'flex',
        'flex-flow': 1,
        height: 58,
        'row-spacing': 4,
    },
    title: {
        'text-color': 0xffffff,
        'font-size': 24,
    },
    subtitle: {
        'text-color': 0x9ca3af,
        'font-size': 14,
    },
    metrics: {
        display: 'grid',
        'grid-template': [[8092, 8092, 8092], [8092]],
        'grid-align': [0, 0],
        'column-spacing': 8,
        height: 78,
    },
    metric: {
        display: 'flex',
        'flex-flow': 1,
        'justify-content': 1,
        'align-items': 1,
        padding: 8,
        'border-radius': 6,
        'background-color': 0x1f2937,
    },
    metricLabel: {
        'text-color': 0x9ca3af,
        'font-size': 12,
    },
    metricValue: {
        'text-color': 0xf9fafb,
        'font-size': 18,
    },
    controls: {
        display: 'flex',
        'flex-flow': 1,
        'row-spacing': 10,
        padding: 12,
        'border-radius': 6,
        'background-color': 0x0f766e,
        'flex-grow': 1,
    },
    sectionTitle: {
        'text-color': 0xffffff,
        'font-size': 16,
    },
    slider: {
        width_pct: 100,
        height: 28,
    },
    action: {
        width_pct: 100,
        height: 42,
        'border-radius': 6,
        'background-color': 0x22c55e,
    },
    actionText: {
        'text-color': 0x052e16,
        'font-size': 16,
    },
    footer: {
        height: 28,
        display: 'flex',
        'justify-content': 1,
        'align-items': 1,
    },
    footerText: {
        'text-color': 0xd1d5db,
        'font-size': 14,
    },
};

Render.render(<App />);

if (globalThis.tjs?.env?.XUAN_LVGL_SAMPLE_EXIT === '1') {
    setTimeout(() => tjs.exit(0), 120);
}
