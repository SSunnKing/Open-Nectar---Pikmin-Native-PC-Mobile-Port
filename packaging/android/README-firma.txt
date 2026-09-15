FIRMA DEL APK (solo para quien publica)

Android exige que todas las versiones de una app vayan firmadas con la misma
clave: un APK con otra clave no se instala encima (hay que desinstalar, y eso
borra los assets extraídos y las partidas). La clave, por tanto, NO cambia
nunca y NO va en el repositorio.

Crear la clave una sola vez (con el JDK de Android Studio):

  mkdir -p ~/.config/opennectar
  ~/Android/jdk/bin/keytool -genkeypair -v \
      -keystore ~/.config/opennectar/release.keystore \
      -alias opennectar -keyalg RSA -keysize 4096 -validity 10950 \
      -dname "CN=Open Nectar, O=Open Nectar"

y escribir ~/.config/opennectar/keystore.properties:

  storeFile=/home/<usuario>/.config/opennectar/release.keystore
  storePassword=<contraseña>
  keyAlias=opennectar
  keyPassword=<contraseña>

Guarda una copia de los dos ficheros fuera del ordenador. Si se pierden, la
siguiente versión no podrá actualizar las instalaciones existentes.

android/app/build.gradle también acepta android/keystore.properties (está en
.gitignore). Sin ninguno de los dos, Gradle firma con la clave debug.
