# Nasu

## Deutsch

**Nasu** ist eine native Medikamenten-Erinnerungs-App für die
**Pebble Time 2** (`emery`).

Wenn ein Medikament fällig ist, zeigt die Uhr die konfigurierten
Tabletten oder den Injektionspen, alarmiert per Ton und/oder Vibration
und wartet auf eine bewusste Bestätigung, bevor die Einnahme als
erledigt markiert wird.

### Die Idee hinter Nasu

Der Name **Nasu** spielt auf das japanische **ナース** (*nāsu*) an –
das japanische Lehnwort für **Nurse / Krankenschwester**.

Die Idee ist einfach: Du hast deine kleine Krankenschwester immer am
Handgelenk dabei. Sie begleitet dich auf ihrer Vespa und erinnert dich
daran, deine Medikamente zu nehmen. Im Alarmfall meldet sie sich direkt,
führt dich zum Einnahmebildschirm und bleibt bei dir, bis die fälligen
Medikamente bestätigt sind.

<p align="center">
  <img src="docs/screenshots/screenshot.png" width="30%" alt="Nasu – screenshot">
  <img src="docs/screenshots/screenshot2.png" width="30%" alt="Nasu – screenshot2">
  <img src="docs/screenshots/screenshot3.png" width="30%" alt="Nasu – screenshot3">
</p>

### Funktionen

- Bis zu **8 Medikamente**
- Tabletten und **Injektionspens**
- Tägliche, wöchentliche und monatliche Einnahmepläne
- Vier konfigurierbare Tageszeiten: Früh, Mittag, Abend und Nacht
- Einstellbare Tablettenform, Farbe, Größe und Beschriftung
- Einstellbare Farben für Pen und Akzent
- Realistische Pillensimulation: Die konfigurierten Medikamente liegen als physikalische Objekte auf dem Erinnerungsbildschirm und reagieren auf die Bewegung der Uhr
- Optionales dezentes japanisches Hintergrundmuster
- **Gedrückthalten zum Bestätigen**
- Scrollen per Touch und Tasten
- Wiederholte Erinnerungen mit einstellbarem Intervall
- Alarmton mit einstellbarer Lautstärke
- Vibration ein/aus
- Oberfläche auf **Deutsch und Englisch**
- **Hell- und Dunkel-Theme**
- Persistenter Erinnerungsstatus
- Pebble-Wakeups

### Funktionsweise

Wird ein Medikament fällig, öffnet Nasu einen eigenen
Erinnerungsbildschirm.

Die Tabletten werden nicht nur als generische Symbole dargestellt:
**Form, Farbe, Größe und Beschriftung können so konfiguriert werden,
dass die Darstellung möglichst dem echten Medikament entspricht.**
Auf der Uhr siehst du damit dieselben Tabletten, die du beim Einnehmen
auch in der Hand hältst.

Im Erinnerungsbildschirm werden die Tabletten in einer kleinen
**Physiksimulation** dargestellt. Sie liegen und bewegen sich auf dem
Display wie reale Tabletten und reagieren auf die Bewegung der Uhr.

Direkt darunter zeigt Nasu eine detaillierte Einnahmeliste. Dort stehen
Name, Wirkung bzw. Beschreibung, Dosierung und Menge des Medikaments –
praktisch, wenn man gerade nicht mehr weiß, welche Tablette welche ist
oder wofür sie gedacht ist.

**Injektionspens werden ausdrücklich unterstützt.** Statt einer Tablette
zeigt Nasu einen animierten Pen mit den konfigurierten Farben. Sind
Tabletten und ein Pen gleichzeitig fällig, werden sie als getrennte
Bestätigungsgruppen angezeigt.

Für Medikamente lassen sich **tägliche, wöchentliche und monatliche
Einnahmepläne** konfigurieren. Dadurch eignet sich Nasu sowohl für
regelmäßige tägliche Medikamente als auch für Präparate, die nur an
bestimmten Wochentagen oder einmal pro Monat genommen werden.

Eine Einnahme wird nicht durch einen kurzen versehentlichen Tastendruck
bestätigt. Stattdessen ist bewusstes Gedrückthalten mit visueller
Rückmeldung erforderlich.

### Konfiguration

Über die Begleit-Konfigurationsseite auf dem Smartphone können
Medikamente verwaltet werden.

Pro Medikament lassen sich unter anderem einstellen:

- Name
- Wirkung / Beschreibung
- Dosierung
- Menge
- Tageszeit
- Täglicher, wöchentlicher oder monatlicher Rhythmus
- Tablette oder Injektionspen
- Aussehen
- Aktiv / inaktiv

Allgemeine Einstellungen:

- Beginn von Früh, Mittag, Abend und Nacht
- Alarmton
- Alarmlautstärke
- Vibration
- Erinnerungsintervall
- Darstellung: Theme, Sprache und japanisches Hintergrundmuster

Bei einer frischen Installation richtet sich die Sprache zunächst nach der
Systemsprache der Pebble. Die Sprache kann anschließend in **Darstellung**
manuell auf Deutsch oder Englisch gestellt werden.

Die Einstellungen werden lokal gespeichert und über
**Pebble AppMessage** an die Uhr übertragen.

### Unterstütztes Gerät

- **Pebble Time 2 (`emery`)**

### Build

```bash
pebble clean
pebble build
```

Installation auf einer verbundenen Uhr:

```bash
pebble install --phone WATCH_IP
```

### Projektstruktur

```text
package.json
resources/
docs/
└── screenshots/
src/
├── c/
│   ├── main.c
│   ├── app_state.c
│   ├── app_util.c
│   ├── watch_settings.c
│   ├── medication_model.c
│   ├── medication_alarm.c
│   ├── medication_ui.c
│   ├── pill_physics.c
│   ├── pill_renderer.c
│   ├── scroll_controller.c
│   └── confirmation_ui.c
└── js/
    └── pebble-js-app.js
```

Weitere Architekturdetails stehen in
[`ARCHITECTURE.md`](ARCHITECTURE.md).

### Medizinischer Hinweis

Nasu ist ein Komfortwerkzeug und **kein Medizinprodukt**.

Verlasse dich nicht ausschließlich auf diese App, wenn ein Medikament
exakt zu einem bestimmten Zeitpunkt eingenommen werden muss oder eine
ausgelassene beziehungsweise falsche Dosis gesundheitliche Folgen
haben kann.

### Lizenz

Nasu ist freie Software unter der
**GNU General Public License v3.0**.

Siehe [`LICENSE`](LICENSE).

Die App enthält den Font **Kosugi Maru Regular**,
Copyright © 2010 The Kosugi Maru Project Authors,
lizenziert unter der **Apache License 2.0**.
Die zugehörige Lizenz liegt unter
[`resources/fonts/LICENSE-KosugiMaru.txt`](resources/fonts/LICENSE-KosugiMaru.txt).

---

## English

**Nasu** is a native medication reminder app for the
**Pebble Time 2** (`emery`).

When medication is due, the watch displays the configured pills or
injection pen, alerts you with sound and/or vibration, and waits for a
deliberate confirmation before marking it as taken.

### The idea behind Nasu

The name **Nasu** is a play on the Japanese **ナース** (*nāsu*) –
the Japanese loanword for **nurse**.

The idea is simple: your little nurse is always with you on your wrist.
She accompanies you on her Vespa and reminds you to take your
medication. When an alarm fires, she leads you to the intake screen and
stays with you until the medication that is due has been confirmed.

<p align="center">
  <img src="docs/screenshots/screenshot.png" width="30%" alt="Nasu – screenshot">
  <img src="docs/screenshots/screenshot2.png" width="30%" alt="Nasu – screenshot2">
  <img src="docs/screenshots/screenshot3.png" width="30%" alt="Nasu – screenshot3">
</p>

### Features

- Up to **8 medications**
- Pills and **injection pens**
- Daily, weekly and monthly schedules
- Four configurable dayparts: morning, noon, evening and night
- Configurable pill shape, color, size and imprint
- Configurable pen body and accent colors
- Realistic pill simulation: configured medications appear as physical objects on the reminder screen and react to movement of the watch
- Optional subtle Japanese background pattern
- Deliberate **hold-to-confirm** interaction
- Touch and button scrolling
- Repeating reminders with configurable intervals
- Alarm sound with configurable volume
- Vibration on/off
- **German and English** interface
- **Light and Dark** themes
- Persistent reminder state
- Pebble wakeups

### How it works

When medication becomes due, Nasu opens a dedicated reminder
screen.

Pills are not shown as generic symbols. **Shape, color, size and imprint
can be configured so the on-screen medication resembles the real pill
as closely as possible.** The goal is that the pills shown on the watch
look like the ones you actually hold in your hand when taking them.

On the reminder screen, the pills are shown in a small **physics
simulation**. They rest and move around the display like physical
tablets and react to movement of the watch.

Directly below the simulation, Nasu shows a detailed intake list with
the medication name, effect or description, dosage and quantity. This
is useful when you no longer remember which pill is which or what it is
for.

**Injection pens are explicitly supported.** Instead of a pill, Nasu
shows an animated pen using the configured colors. If pills and a pen
are due at the same time, they are presented as separate confirmation
groups.

Medication can be scheduled **daily, weekly or monthly**, so Nasu works
both for regular everyday medication and for treatments that are only
taken on selected weekdays or once per month.

Medication is not marked as taken by a short accidental button press.
Confirmation requires a deliberate press-and-hold action with visual
feedback.

### Configuration

The companion configuration page lets you manage medications from your
phone.

For each medication you can configure:

- Name
- Effect / description
- Dosage
- Quantity
- Daypart
- Daily, weekly or monthly schedule
- Pill or injection pen
- Appearance
- Active / inactive state

General settings include:

- Morning, noon, evening and night start times
- Alarm sound
- Alarm volume
- Vibration
- Reminder interval
- Appearance: theme, language and Japanese background pattern

On a fresh installation, the app initially follows the Pebble system
language. Language can then be set manually to German or English under
**Appearance**.

Settings are stored locally and transferred to the watch through
**Pebble AppMessage**.

### Supported device

- **Pebble Time 2 (`emery`)**

### Build

```bash
pebble clean
pebble build
```

Install on a connected watch:

```bash
pebble install --phone WATCH_IP
```

### Project structure

```text
package.json
resources/
docs/
└── screenshots/
src/
├── c/
│   ├── main.c
│   ├── app_state.c
│   ├── app_util.c
│   ├── watch_settings.c
│   ├── medication_model.c
│   ├── medication_alarm.c
│   ├── medication_ui.c
│   ├── pill_physics.c
│   ├── pill_renderer.c
│   ├── scroll_controller.c
│   └── confirmation_ui.c
└── js/
    └── pebble-js-app.js
```

More details about the internal structure can be found in
[`ARCHITECTURE.md`](ARCHITECTURE.md).

### Medical disclaimer

Nasu is a convenience tool and is **not a medical device**.

Do not rely on this app as the only safeguard for medication that must
be taken at an exact time or where a missed or incorrect dose could
cause harm.

### License

Nasu is free software released under the
**GNU General Public License v3.0**.

See [`LICENSE`](LICENSE).

The app includes **Kosugi Maru Regular**,
Copyright © 2010 The Kosugi Maru Project Authors,
licensed under the **Apache License 2.0**.
The corresponding license is included at
[`resources/fonts/LICENSE-KosugiMaru.txt`](resources/fonts/LICENSE-KosugiMaru.txt).
