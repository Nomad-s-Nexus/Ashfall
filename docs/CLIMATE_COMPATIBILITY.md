# Climate Compatibility

The climate system does not require FormLists and does not modify ESP/ESL records.

## Armor insulation

Default armor protection is automatic:

1. Read the worn armor's authored biped coverage.
2. Apply the default 45% regional warmth.
3. Combine overlapping layers with diminishing returns.
4. Apply JSON overrides only when the automatic result needs correction.

Override precedence is:

`exact armor > first matching keyword rule > automatic default`

Configuration file:

`Data/F4SE/Plugins/FalloutAnomaly/climate_protection.json`

Example:

```json
{
  "version": 1,
  "armorOverrides": [
    {
      "plugin": "ExampleArmor.esp",
      "form": "0x1234",
      "insulation": 85
    }
  ],
  "keywordOverrides": [
    {
      "plugin": "ExampleArmor.esp",
      "form": "0x5678",
      "insulation": 70
    }
  ]
}
```

`insulation` is regional warmth from 0 to 100. It applies only to the body regions the armor record actually covers.

Use exact armor overrides for one-off exceptions. Use keyword overrides when a mod already marks a family of armor with a useful keyword. Keyword rules are evaluated in file order, so put more specific categories before broader categories.

All plugin/local FormIDs are resolved once when the climate protection configuration loads. The 4 Hz climate update does not parse JSON or search the load order. Worn armor itself is rescanned only after a player armor equip/unequip event invalidates the cache.

## Heat sources

Vanilla flaming cooking fires and bonfire barrels have built-in heat profiles. Mod-added heat objects can be registered without a compatibility plugin.

Configuration file:

`Data/F4SE/Plugins/FalloutAnomaly/climate_heat.json`

Example:

```json
{
  "version": 1,
  "heatSources": [
    {
      "plugin": "ExampleCampfires.esp",
      "form": "0x1234",
      "radius": 900,
      "strength": 70
    }
  ]
}
```

`radius` is in Fallout 4 game units. `strength` is 0 to 100.

Heat discovery is cache-driven. Nearby references are rescanned after entering/changing cells, moving at least 96 game units, or an object load/unload event invalidates the cache. A stable stationary scene does not trigger periodic heat scans.

## Weather behavior

Weather temperature uses the weather record's authored classification flags before any future exact-record override layer:

- Pleasant: small warmth bonus.
- Cloudy: mild cooling.
- Rainy: stronger cooling and full precipitation wetness.
- Snow: strongest cooling and reduced-rate precipitation wetness.

Interiors ignore exterior weather temperature and precipitation.

## Runtime rules

- No climate compatibility FormLists are required.
- No plugin records are created or modified.
- Missing or invalid individual JSON entries fail soft.
- Automatic behavior remains active when override files are empty or absent.
- Power Armor uses its separate complete-suit rule rather than JSON armor warmth for the sealed-suit floor.
