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
| `tests/` | Tests (`make test`) et simulateur de lecteur (`tests/sim.h`). |

## Utilisation

```sh
accuchek [NUMERO_LECTEUR] > mesures.json      # lecteur branché
accuchek --capture lecture.trace > mesures.json  # idem, en enregistrant les échanges USB
accuchek --replay lecture.trace > mesures.json   # rejoue une trace, sans lecteur ni root
```

Une trace contient les mesures du lecteur : ce sont des données de santé, à garder hors du dépôt (`~/.local/share/glucofi/traces/`). Le hook pre-commit refuse les `*.trace` hors de `tests/fixtures/`.

## Compiler et tester

Dépendances Mageia : `gcc-c++`, `make`, `lib64usb1.0-devel`. Facultatif pour les tests : `libasan-devel` et `libubsan-devel` (activés automatiquement s'ils sont installés).

```sh
make -C services/device/accuchek-src          # binaire
make -C services/device/accuchek-src test     # tests, lancés aussi par scripts/gate.sh
python3 -m evals.accuchek_replay              # eval : rejoue toutes les traces connues
```

Les tests de session rejouent des traces produites par le simulateur (`tests/sim.h`), qui construit les paquets du lecteur avec la même disposition que le pilote Tidepool. `tests/fixtures/two_segments.trace` en est une copie versionnée ; après une modification du simulateur : `ACCUCHEK_UPDATE_FIXTURES=1 make test`.
