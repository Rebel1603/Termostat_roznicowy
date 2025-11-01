Termostat różnicowy działający pod Supla - długo oczekiwany przeze mnie projekt - wykonałem z pomocą kolegów z Supla - Chwała im !. 
Porównywanie dwóch temperatur i przełączenie przekaźnika. W zależności od trybu grzanie/chłodzenie logika jest odwracana. W konfigu mozna ustawić histerezę różnicy temepartur co 0.1°C oraz czas przełączania/kontroli różnicy temperatur. W trybie Auto układ działa automatycznie w zależności od nastaw. Tryb manualny pozwala na twarde przełaczenie ON/OFF przekaźnika na dedykowanym GPIO. 
W Folderze znajduje się kompilacja dla ESP32 Dev oraz ESP32-C3 wraz z opisem pinów GPIO. 
W razie chęci uproszczenia widoku w Apce Supla wystarczy w urządzeniach klienckich wyłaczyć nieużywane przełączniki - funkcjonalność zostanie zachowana a widok skrócony. Oczywiście ma to sens dla automatycznego trybu (wszystkie przełaczniki na off). 

Schemat skrócony działania :
jeśli TRYB_Auto:
  jeśli Chłodzenie & (Tout < Tin - histereza) { relay ON }    // włącz wentylację gdy na polu chłodniej
  jeśli Grzanie & (Tout > Tin + histereza) { relay ON }    // włącz wentylację gdy na polu cieplej  
jeśli TRYB_Manual:
  jeśli Manual == OFF  { relay OFF }    // wyłącz wentylację ; 
  jeśli Manual == ON  { relay ON }    // włącz wentylację


