# ESP32 OpenThread AWS Temperature Node

Przykładowa aplikacja dla układów ESP32 (np. ESP32-C6/H2) demonstrująca działanie węzła sieci OpenThread, który przesyła dane telemetryczne do chmury AWS.

Projekt odczytuje wewnętrzną temperaturę układu i wysyła ją do AWS (np. API Gateway / Lambda) za pomocą bezpiecznych żądań HTTP (PUT), podpisanych metodą **AWS Signature v4**.

## 🚀 Główne funkcjonalności

- **OpenThread Stack:** Pełna inicjalizacja stosu sieciowego Thread wraz z CLI.
- **AWS SigV4:** Ręczne podpisywanie żądań HTTP w C++ (wymagana klasa `AWSSigner`).
- **Telemetria:** Odczyt temperatury z wbudowanego czujnika wewnętrznego ESP32.
- **Synchronizacja Czasu (SNTP):** Automatyczne pobieranie czasu z `pool.ntp.org` (niezbędne do autoryzacji w AWS).
- **Bezpieczeństwo:** Obsługa HTTPS z weryfikacją certyfikatów (`esp_crt_bundle`).

## ⚙️ Konfiguracja

Przed kompilacją **musisz** uzupełnić swoje poświadczenia AWS w pliku źródłowym, w funkcji `send_