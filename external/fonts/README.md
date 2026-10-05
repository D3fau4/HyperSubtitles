# Fuentes incluidas

`NotoSansJP-Regular.otf`: Noto Sans JP 2.004 (de
[notofonts/noto-cjk](https://github.com/notofonts/noto-cjk), `Sans/SubsetOTF/JP`),
recortada a los caracteres de cp932 más los que aparecen en `data/lines/*.json`.
El editor la lleva incrustada para mostrar el texto japonés en cualquier PC.
No se usa en el juego ni en la preview de la caja.

Licencia: SIL Open Font License 1.1 (`OFL.txt`).

Para regenerarla (necesita `pip install fonttools`):

```sh
curl -LO https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/SubsetOTF/JP/NotoSansJP-Regular.otf
python tools/fonts/subset_japanese.py NotoSansJP-Regular.otf
```
