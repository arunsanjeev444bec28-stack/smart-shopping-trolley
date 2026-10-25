# Smart Shopping Trolley

An ESP32-based smart shopping trolley prototype featuring RFID product identification, OLED display, load-cell monitoring, checkout, a fixed UPI QR code, and a Python billing dashboard.

## Features

- RFID-based product identification
- Automatic quantity and total calculation
- OLED status and checkout display
- Load-cell monitoring
- Checkout bill generation
- Browser-based billing dashboard
- Local SQLite database

## Requirements

- Windows 10 or Windows 11 laptop
- Python 3.12 or another tested compatible Python version
- ESP32 development board
- Arduino IDE
- Required Arduino libraries: MFRC522, Adafruit GFX, Adafruit SSD1306, and HX711

## Installation

1. Download or clone this repository.
2. Install Python and ensure the Python launcher is available.
3. Open the backend folder in File Explorer.
4. Create a private `config.json` based on `config.example.json`.
5. Set a unique dashboard password and device token.
6. Open Command Prompt in the backend folder.
7. Initialize the database:

   `py db.py`

8. Start the server:

   `py server.py`

9. Open the dashboard in your browser:

   `http://localhost:8000`

10. Sign in with the dashboard credentials from your private configuration.

## Testing

Run the backend test script in a separate Command Prompt:

`py test_send.py`

The test submits a sample checkout to the local server. Use a test database when verifying installation.

## Arduino Setup

1. Open `firmware/smarttrolley.ino` in Arduino IDE.
2. Ensure `upi_qr_bitmap.h` is available in the same sketch folder.
3. Install the required Arduino libraries.
4. Select the correct ESP32 board and COM port.
5. Upload the sketch and test RFID scans, the OLED, checkout, and QR display.

## Security

The included local HTTP server is intended for a controlled local demonstration. Do not expose it directly to the public internet. Use strong private credentials and a secure authenticated deployment for remote access.

## Payment Notice

The fixed UPI QR displays a payment destination. It does not verify whether payment has succeeded. Confirm payments separately in the merchant's UPI application.

## Project Status

Prototype software. Verify all hardware, database, security, and deployment behaviour before commercial use.
