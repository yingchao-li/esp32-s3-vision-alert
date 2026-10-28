
#define BUZZER_PIN 14  // Freenove ESP32-S3 GPIO14

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("Buzzer Ready!");
}

void loop() {
  Serial.println("Beep! Beep!");

  // First beep
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  delay(200);

  // Second beep
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);

  // Stop for 2 seconds
  delay(2000);
}
