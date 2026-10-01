# services/device/accuchek-src

Code source de `accuchek`, le programme C++ qui lit les mesures d'un Accu-Chek Guide en USB (libusb) et les écrit en JSON sur stdout.

- Origine : <https://github.com/emogenet/accuchek>, commit `79dd4d1` (5 décembre 2024). Licence : domaine public (`LICENSE.txt`, Unlicense).
- README d'origine : `README.upstream.md`.
- Compilé et installé dans `/usr/local/bin/accuchek` par `packaging/install-system.sh`.

## Organisation

| Fichier | Rôle |
| --- | --- |
| `protocol.h/.cpp` | Constantes ISO/IEEE 11073, construction des messages envoyés, décodage des messages reçus (config, segments, dates BCD), JSON. Aucune entrée/sortie. |
| `session.h/.cpp` | Déroulé complet d'une lecture sur un `Transport` abstrait. Les échecs lèvent `SessionError`. |
| `trace.h/.cpp` | Traces d'échanges USB : `RecordingTransport` (`--capture`) et `ReplayTransport` (`--replay`). |
| `main.cpp` | Ligne de commande, détection USB (libusb), `LibusbTransport`, sortie JSON. |
| `tests/` | Tests (`make test`), simulateur de lecteur (`tests/sim.h`), fuzzer (`tests/fuzz.cpp`). |

## Utilisation

```sh
accuchek [NUMERO_LECTEUR] > mesures.json      # lecteur branché
accuchek --capture lecture.trace > mesures.json  # idem, en enregistrant les échanges USB
accuchek --replay lecture.trace > mesures.json   # rejoue une trace, sans lecteur
accuchek --known-devices                         # lecteurs acceptés (vendor:product)
```

Aucune commande ne demande root. L'accès USB au lecteur vient de la règle udev `packaging/udev/70-glucofi-accuchek.rules` (`TAG+="uaccess"`, pour la session locale active) ; sans elle, accuchek sort en code 3 (`permission denied on USB meter ...`). L'original refusait de tourner si `euid != 0`.

Les lecteurs acceptés sont intégrés au programme (`defaultConfig()` : `173a:21d5` Accu-Chek Guide, `173a:21d7`, `173a:21d8` Relion Platinum). Aucun fichier n'est lu dans le dossier courant. Pour essayer un autre modèle ou en désactiver un : `accuchek --config fichier.txt`, au format de `config.example.txt` (`vendor_0x173a_device_0x21d7 0` désactive).

Une trace contient les mesures du lecteur : ce sont des données de santé, à garder hors du dépôt (`~/.local/share/glucofi/traces/`). Le hook pre-commit refuse les `*.trace` hors de `tests/fixtures/`.

## Sortie

Un tableau JSON, une mesure par objet : `id`, `epoch`, `timestamp` (heure du lecteur), `mg/dL`, `mmol/L`, `status` (statut brut du lecteur). Toutes les mesures du lecteur sont écrites, quel que soit leur statut. Les lectures hors échelle (valeurs spéciales `0x07FE` et `0x0802`, comme dans le pilote Tidepool) ont `"range":"high"` avec 601 mg/dL ou `"range":"low"` avec 9 mg/dL. Contrat : `contracts/accuchek_output.schema.json`.

Le JSON est écrit en une seule fois, une fois la lecture terminée. En cas d'échec, stdout reste vide (aucune mesure partielle) et stderr contient une ligne `accuchek: <raison>`. Avec `ACCUCHEK_DBG=1`, les logs et dumps hexadécimaux vont sur stderr ; stdout ne change pas.

| Code | `ExitCode` (`session.h`) | `contracts.AccuchekExit` | Cas |
| --- | --- | --- | --- |
| 0 | `kExitOk` | `OK` | lecture réussie, y compris lecteur vide (`[]`) ou libération ratée après la dernière mesure |
| 1 | `kExitUsage` | `USAGE` | option inconnue, fichier de config, trace ou capture illisible |
| 2 | `kExitNoDevice` | `NO_DEVICE` | aucun lecteur connu sur le bus USB |
| 3 | `kExitAccessDenied` | `ACCESS_DENIED` | lecteur trouvé mais ouverture refusée (règle udev absente) |
| 4 | `kExitTransfer` | `TRANSFER` | transfert USB échoué : timeout, lecteur débranché |
| 5 | `kExitProtocol` | `PROTOCOL` | le lecteur a interrompu l'association ou répondu autre chose |

## Compiler et tester

Dépendances Mageia : `gcc-c++`, `make`, `lib64usb1.0-devel`. Facultatif pour les tests : `libasan-devel` et `libubsan-devel` (activés automatiquement s'ils sont installés).

```sh
make -C services/device/accuchek-src          # binaire
make -C services/device/accuchek-src test     # tests, lancés aussi par scripts/gate.sh
make -C services/device/accuchek-src fuzz     # eval : 200 000 paquets mutés, 0 plantage attendu
python3 -m evals.accuchek_replay              # eval : rejoue toutes les traces connues
python3 -m evals.accuchek_errors              # eval : pannes injectées dans chaque trace
```

Les tests de session rejouent des traces produites par le simulateur (`tests/sim.h`), qui construit les paquets du lecteur avec la même disposition que le pilote Tidepool. `tests/fixtures/two_segments.trace` en est une copie versionnée ; après une modification du simulateur : `ACCUCHEK_UPDATE_FIXTURES=1 make test`.

## Lecture des messages reçus

Tous les décodeurs (`readInvokeId`, `parseConfigInfo`, `parseSegment`) reçoivent le nombre d'octets réellement reçus et lisent via `Reader`, qui refuse de dépasser la fin : un message tronqué ou incohérent (segment qui annonce plus de mesures qu'il n'en contient, taille d'objet au-delà de la fin) arrête la lecture avec un message clair. Une date BCD invalide ne bloque pas la lecture : la mesure sort avec `"error":"invalid date"` et Glucofi l'écarte en donnant le motif.

Le fuzzer place chaque paquet muté juste avant une page mémoire protégée : lire un seul octet de trop provoque un plantage, même sans AddressSanitizer. En cas de plantage il affiche la commande pour rejouer l'itération (`FUZZ_ARGS="--seed S --from N --count 1" make fuzz`).
