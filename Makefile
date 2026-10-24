PIO = $(HOME)/.platformio/penv/bin/pio

# PlatformIOのバージョン確認（未インストールなら公式手順で入れる）
ready:
	$(PIO) --version

# ビルド
build:
	$(PIO) run

# ビルド + M5Stackへのアップロード
upload:
	$(PIO) run --target upload

# シリアルモニタ起動
monitor:
	$(PIO) device monitor
