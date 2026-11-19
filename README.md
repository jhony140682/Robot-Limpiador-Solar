# Robot-Limpiador-Solar
Proyecto de ingeniería mecatrónica enfocado en la automatización de la limpieza de paneles solares mediante dos microcontroladores ESP32, comunicados a través de Wi-Fi y Firebase Realtime Database.

ESP32 principal

Se encarga de calcular la potencia estimada y el rendimiento del panel solar, controlar los motores, gestionar la secuencia de limpieza y enviar las instrucciones de activación de la bomba al ESP32 externo.

ESP32 externo

Se encarga de medir la corriente mediante el sensor ACS712, enviar las mediciones a Firebase y controlar la bomba de agua según las instrucciones recibidas.

Comunicación y funcionamiento

Ambos microcontroladores intercambian información mediante Firebase para coordinar el proceso de limpieza. El sistema también cuenta con una interfaz web que permite visualizar las mediciones, consultar el estado del robot, modificar el umbral de rendimiento y realizar el rearme remoto.

Por motivos de seguridad, no se incluyen las credenciales de acceso ni los archivos auxiliares de configuración.
