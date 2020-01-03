int p = 25;

uint8_t row[] = { 34, 35, 32, 33, 25 };    // +
uint8_t col[] = { /* groen */ 4, 17, 18, /* rood */ 16, 5, 19 };  // n fet

void testmatrix(int ms) {
  for (int c = 0; c < sizeof(col); c++) {
    digitalWrite(col[c], HIGH);
    for (int r = 0; r < sizeof(row); r++) {
      if (ms) Serial.printf("%d, %d (IO %d, %d)\n", r, c, row[r], col[c]);
      digitalWrite(row[r], HIGH);
      delay(ms);
      digitalWrite(row[r], LOW);
    }
    digitalWrite(col[c], LOW);
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("meh");
  pinMode(p, OUTPUT);
  for (int r = 0; r < sizeof(row); r++) pinMode(row[r], OUTPUT);  
  for (int c = 0; c < sizeof(col); c++) pinMode(col[c], OUTPUT);
}

void loop() {
  // put your main code here, to run repeatedly:
  static int i = 0;
  Serial.println(i++);
  
  testmatrix(400);
  unsigned long end = millis() + 1000;
  while (millis() < end) testmatrix(0);
    
}
