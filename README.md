# Module audio

This module utilizes portaudio to record and output audio.

## Supported Platforms
- **Darwin ARM64**
- **Linux x64**:
- **Linux ARM64**:

## Model viam:audio:microphone

### Configuration
The following attribute template can be used to configure this model:

```json
{
  "device_name" : <DEVICE_NAME>,
  "sample_rate": <SAMPLE_RATE>,
  "num_channels": <NUM_CHANNELS>,
  "latency": <LATENCY>
}
```
#### Configuration Attributes

The following attributes are available for the Astra 2 model:

| Name          | Type   | Inclusion | Description                |
|---------------|--------|-----------|----------------------------|
| `device_name` | string | **Optional** | The PortAudio device name to stream audio from. If not specified, the system default will be used. |
| `sample_rate` | int | **Optional** | The sample rate in Hz of the stream. If not specified, the device's default sample rate will be used. |
| `num_channels` | int | **Optional** | The number of audio channels to capture. Must not exceed the device's maximum input channels. Default: 1 |
| `latency` | int | **Optional** | Suggested input latency in milliseconds. This controls how much audio PortAudio buffers before making it available. Lower values (5-20ms) provide more responsive audio capture but use more CPU time. Higher values (50-100ms) are more stable but less responsive. If not specified, uses the device's default low latency setting (typically 10-20ms). |


## Setup
```bash
canon make setup
```

## Build Module
```bash
canon make
```

## Build (Development)
```bash
canon make build
```
