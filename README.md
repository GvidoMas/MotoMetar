# MotoMetar

MotoMetar je sustav koji simulira rad spirometra upotrebom potenciometra te sam proces mjerenja pretvara u igru radi motivacije korisnika.
Snaga puhanja simulira se potenciometrom te ona regulira vožnju motora. Cilj je motor održavati na zadnjem kotaču.

## Opis projekta

Igra funkcionira na način da u početku motor stoji.
Laganim puhanjem (600ml/s) se počinje kretati.
Kada snaga puhanja dosegne 900ml/s motor se podiže na zadnji kotač.
Ukoliko snaga puhanja premaši maksimum od 1200ml/s motor se nagne previše unatrag i padne što označava neuspjeli pokušaj.
Do neuspjelog pokušaja također može doći i ako snaga puhanja padne ispod 900ml/s čime se prednji kotač motora spušta na tlo. 
Ukoliko se motor uspije održavati na zadnjem kotaču dulje od 5 sekundi test je uspješno odrađen.

Za izradu uređaja koristit će se esp32, OLED zaslon i potenciometar.

# Funkcijski zahtjevi
 - Očitavanje ulaznog signala
 - Određivanje smjera disanja
 - Filtriranje signala
 - Izračun protoka
 - Izračun volumena
 - Prikaz rezultata
 - Vizualizacija protoka kroz igru
 - Detekcija uspjeha i neuspjeha
 - Pohrana rezultata

# Tehnologije
ESP32 - glavna upravljačka jedinica sustava
OLED zaslon - korišten za prikaz korisničkog sučelja
Potenciometar - simulacija protoka zraka (udisaj i izdisaj)
C++ - programski jezik korišten za realizaciju projekta
Wokwi - simulator korišten za realizaciju projekta


# Članovi tima 
Hrvoje Sić\n
Gvido Maskalan

# Kontribucije
Hrvoje Sić
 - Očitavanje ulaznog signala
 - Određivanje smjera disanja
 - Filtriranje signala
 - Izračun protoka
 - Izračun volumena

Gvido Maskalan
 - Prikaz rezultata
 - Vizualizacija protoka kroz igru
 - Detekcija uspjeha i neuspjeha
 - Pohrana rezultata
