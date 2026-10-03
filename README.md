# Health Tracker

A small C++ core for talking to the [Google Health API](https://developers.google.com/health),
built for an ESP32 with a color TFT display that shows daily steps and
active zone minutes. The desktop build is just a one-time OAuth setup tool (`--auth`); it shares its
OAuth/HTTP/JSON code with the ESP32 build but doesn't fetch or display data itself.


<img width="2880" height="2160" alt="IMG_8221" src="https://github.com/user-attachments/assets/938b07b1-3b6e-4568-874d-7e3798215b2e" />


## 1. Google Cloud setup (one-time)

1. Create a project in the [Google Cloud Console](https://console.cloud.google.com/).
2. Enable the **Google Health API** for that project.
3. Go to **APIs & Services > Credentials** and create an **OAuth client ID**
   of type **Web application**. Add `https://www.google.com` as an
   authorized redirect URI (this tool uses the manual copy/paste flow — see
   below — so there's no local server to redirect to).
4. If your app is in "Testing" publishing status, add your own Google
   account under **Audience > Test users**, or you'll get an access-denied
   screen during consent.
5. Note the **Client ID** and **Client secret**.

## 2. Configure credentials

```sh
cp .env.example .env
```

Fill in `GOOGLE_CLIENT_ID` and `GOOGLE_CLIENT_SECRET` in `.env`.

The Google Health API uses OAuth 2.0, not a static API key: alongside the
client ID/secret you also need a **refresh token**, which the app obtains
for you in the next step.

## 3. Build

Dependencies: a C++17 compiler, CMake (>= 3.14), and libcurl development
headers. `nlohmann/json` is used for JSON; if it's not already installed as
a system package, CMake will download it automatically on first configure.

```sh
sudo apt install -y build-essential cmake libcurl4-openssl-dev nlohmann-json3-dev
cmake -B build
cmake --build build
```

## 4. Authorize (one-time)

```sh
./build/health_tracker
```

This prints a Google consent URL. Open it, sign in, and grant access. Google
redirects to `https://www.google.com/?code=...`. Copy the full URL (or just the `code=` value) from
your browser's address bar and paste it back into the terminal. The tool
exchanges it for a refresh token and saves it to `.env` automatically.

The scopes requested are computed from `DATA_TYPES` in `.env` (default:
`steps,active-energy-burned,heart-rate,sleep,exercise`) — see
`include/healthtracker/data_types.hpp` for the full list of supported data
types. Re-run this if you revoke access or change `DATA_TYPES`.

## ESP32

`esp32/` is a PlatformIO project targeting a generic ESP32 dev board
(`esp32dev`, which covers the Elegoo USB-C board) with a 2.2" 240x320
ILI9341 SPI TFT display, using the Arduino framework. It connects to WiFi,
syncs time over NTP, and then every 10 minutes fetches today's steps and
active zone minutes and redraws them as progress bars/rings on the TFT. It should function
indefinitely and automatically retry the wifi connection, restart wifi drivers, and handle midnight rollover errors gracefully. Worst case if something fails power-cycle the board until the display powers back on.

### Wiring

The display connects over SPI, remapped off the ESP32's default VSPI pins to
match the actual wiring (see `TFT_*_PIN` constants in `esp32/src/main.cpp`).
No RST line is wired, so the driver does a software reset instead.

| TFT pin | ESP32 pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SCK | GPIO18 |
| MOSI (SDI) | GPIO22 |
| CS | GPIO5 |
| DC | GPIO2 |
| RST | not connected |

### Setup

1. Install [PlatformIO](https://platformio.org/install) (e.g. the VS Code
   extension, or `pip install platformio` for the CLI).
2. You need a refresh token already — if you haven't, run the desktop
   `--auth` flow first (steps 1–4 above).
3. ```sh
   cp esp32/include/secrets.h.example esp32/include/secrets.h
   ```
   Fill in your WiFi SSID/password, `GOOGLE_CLIENT_ID`/`GOOGLE_CLIENT_SECRET`/
   `GOOGLE_REFRESH_TOKEN` (copy these three straight from your working
   `.env`), and your UTC offset (`GMT_OFFSET_SEC`/`DAYLIGHT_OFFSET_SEC` — the
   ESP32 has no IANA timezone database, so unlike the desktop build this is a
   fixed offset you set once). `secrets.h` is gitignored.
4. Build and flash (CLI):
   ```sh
   cd esp32
   pio run --target upload
   pio device monitor
   ```
   Or open the `esp32/` folder in VS Code with the PlatformIO extension and
   use its Build/Upload/Monitor buttons.

**Important**: Ensure you publish the consent screen to Production in the Google Cloud Console or else the refresh token will expire in 7 days.

