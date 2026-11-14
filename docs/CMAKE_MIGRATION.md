# Переход Yate на CMake для разработчиков

Руководство описывает сборку и дальнейшую разработку текущего форка Yate 6.4.1 после удаления Windows, Qt4 и H.323. Оно предназначено для разработчиков ядра, внутренних библиотек, плагинов и пакетных сценариев.

**Основной путь сборки — независимый запуск CMake.** Запускать `autogen.sh` и `configure` перед ним не требуется. Autoconf и `Makefile.in` пока оставлены для сравнения и существующей упаковки; миграция RPM/Gentoo ещё не завершена.

Точка входа — [корневой CMakeLists.txt](../CMakeLists.txt). Описанные параметры и примеры соответствуют ветке `codex/cmake-migration` на 14 ноября 2026 года. Исходная база — коммит `6c9170e036d00736f8963caf8b6d0183c86da5bc` официального Yate.

## Содержание

1. [Состояние переноса и границы проверки](#1-состояние-переноса-и-границы-проверки)
2. [Подготовка окружения](#2-подготовка-окружения)
3. [Первый запуск CMake](#3-первый-запуск-cmake)
4. [Соответствие старых и новых команд](#4-соответствие-старых-и-новых-команд)
5. [Параметры сборки](#5-параметры-сборки)
6. [Зависимости и состав модулей](#6-зависимости-и-состав-модулей)
7. [Готовые профили](#7-готовые-профили)
8. [Структура сборочной системы](#8-структура-сборочной-системы)
9. [Добавление модуля](#9-добавление-модуля)
10. [Добавление библиотеки и зависимости](#10-добавление-библиотеки-и-зависимости)
11. [Внешние модули и установленный SDK](#11-внешние-модули-и-установленный-sdk)
12. [Установка и конфигурация](#12-установка-и-конфигурация)
13. [Тестирование и отладка](#13-тестирование-и-отладка)
14. [Перенос изменений из своего форка](#14-перенос-изменений-из-своего-форка)
15. [Пакеты и завершение миграции](#15-пакеты-и-завершение-миграции)
16. [Диагностика](#16-диагностика)
17. [Проверка изменений перед слиянием](#17-проверка-изменений-перед-слиянием)
18. [Справочные материалы](#18-справочные-материалы)

## 1. Состояние переноса и границы проверки

В CMake описаны ядро, внутренние библиотеки и 94 оставшиеся цели рабочих модулей. В проверенном окружении доступны 84 рабочих модуля и четыре исходных тестовых плагина. Количество зависит от установленных библиотек и выбранных опций; число 84 не является обязательным результатом на другой машине.

Проверенное окружение: Oracle Linux 8.10 в WSL, x86_64, GCC 8.5.0, CMake 3.26.5. Минимальная версия, объявленная проектом, — CMake 3.16; вся матрица проверялась на 3.26.5.

| Проверка | Результат |
| --- | --- |
| Release с доступными зависимостями | 84 рабочих и 4 тестовых модуля; 5/5 CTest |
| Debug с альтернативными реализациями | 5/5 CTest |
| Сборка без модулей | 2/2 CTest |
| Явный выбор четырёх SIP/RTP-модулей | Состав файлов совпал с выбранным набором; тесты прошли |
| Установка и `DESTDIR` | Проверены, включая SIP/RTP из установленной копии |
| Повторная установка | Настройки администратора сохранены |
| SDK | Собраны внешний модуль и программа; проверен `pkg-config` |
| Совместимость с исходной сборкой | Совпал исходный набор 79 модулей; сохранены сильные экспортируемые символы ядра |
| Пути с пробелами и кириллицей | Проверены исходники; отдельно проверены сборочный и установочный каталоги с пробелами |

Совпадение экспортируемых символов не является полной проверкой ABI. При изменении RTTI, публичных макросов, архитектуры или toolchain пересобирайте и проверяйте зависимые модули.

На macOS и FreeBSD сборка не запускалась. Реальные платы, радиооборудование, внешние серверы БД и нагрузочные сценарии требуют отдельной приёмки. Без необходимых зависимостей здесь не собирались `coreaudio`, `zapcard`, `wpcard`, `tdmcard`, `faxchan`, `gsmcodec`, `speexcodec`, `amrnbcodec`, `efrcodec`, `ybladerf`.

Удалены собственные ветки поддержки Windows, DirectSound и Windows Wanpipe, клиент и плагины Qt4, формы Qt Designer, H.323/OpenH323/PWLib и связанные настройки. Общий клиентский API ядра сохранён: он не равен реализации Qt4. Атрибуты RADIUS с именами `h323-*` также сохранены — они применяются и при SIP-биллинге.

## 2. Подготовка окружения

Все команды `sh` ниже выполняются в **Bash/Linux из корня репозитория**, где лежит `CMakeLists.txt`. Они не предназначены для непосредственного запуска в PowerShell.

На текущем Windows-компьютере сначала откройте WSL:

```powershell
wsl -d OracleLinux_8_10
```

Затем в Linux:

```sh
cd '/mnt/c/Users/User/Documents/ChatGPT/Переход yate на cmake/yate'
```

Для сборки нужны CMake 3.16+, компиляторы C/C++ с C++11 и GNU-совместимыми параметрами, POSIX threads и Make либо Ninja. `pkg-config` нужен для зависимостей, поиск которых его использует; Python 3.6+ — для интеграционных тестов. Doxygen нужен только при включённой API-документации.

Проверьте инструменты:

```sh
cmake --version
cc --version
c++ --version
make --version
pkg-config --version
python3 --version
```

Нужны пакеты разработки зависимостей: наличие одной runtime-библиотеки без заголовков не гарантирует возможность собрать модуль. Фактический набор зависимостей Linux CI указан в [.github/workflows/cmake.yml](../.github/workflows/cmake.yml). Локальные проверенные пакеты Oracle Linux включали `libpq-devel`, `mariadb-connector-c-devel`, `sqlite-devel`, `alsa-lib-devel`, `lksctp-tools-devel`; имена пакетов в других дистрибутивах отличаются.

После установки недостающих пакетов используйте новый каталог сборки: результаты некоторых проб компилятора CMake сохраняет в кэше.

## 3. Первый запуск CMake

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-release --parallel 4
(cd build-release && ctest --output-on-failure)
./build-release/run --version
```

`-S` задаёт исходники, `-B` — отдельный каталог сборки. Конфигурация, компиляция и запуск тестов — отдельные операции. Общий интерфейс команд описан в [руководстве cmake 3.16](https://cmake.org/cmake/help/v3.16/manual/cmake.1.html).

Сборка непосредственно в исходниках запрещена проектом. Не используйте `cmake .`. В одном каталоге сборки также не следует менять компилятор, архитектуру или генератор: создайте другой каталог.

Для Ninja, если он установлен:

```sh
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja --parallel 4
(cd build-ninja && ctest --output-on-failure)
```

В конце настройки проверяйте строки `Enabled modules` и `Unavailable optional modules`. Успех `cmake` сам по себе не означает, что собран именно требуемый продукту набор.

Основные результаты сборки:

```text
build-release/
  yate
  libyate.so.6.4.1
  libyatescript.so.6.4.1
  modules/
    ysipchan.yate
    server/pgsqldb.yate
    sig/camel_map.yate
    test/crypto.yate
  conf.d/
  run
  yate-config
  yate.pc
  yateversn.h
  yatepaths.h
  compile_commands.json
```

Наличие конкретных библиотек и модулей зависит от параметров. Сгенерированные заголовки, скрипты и `CMakeCache.txt` вручную не редактируют.

## 4. Соответствие старых и новых команд

| Прежнее действие | Действие в CMake |
| --- | --- |
| `./autogen.sh`, затем `./configure` | `cmake -S . -B build-release ...` |
| `make -j4` | `cmake --build build-release --parallel 4` |
| `make engine` | `cmake --build build-release --target yate` |
| Собрать только `libyate` | `cmake --build build-release --target libyate` |
| Собрать выбранный модуль | `cmake --build build-release --target ysipchan` |
| `make libs` | Общая сборка либо перечисление библиотечных целей; отдельная переносимая цель `libs` не добавлена |
| `make modules` | Общая сборка всех выбранных модулей; отдельная переносимая цель `modules` не добавлена |
| `make test` | Для сборки исходных тестовых плагинов — `YATE_BUILD_TEST_MODULES=ON`; для исполнения проверок — CTest |
| `make debug` / `make ddebug` | Отдельный каталог с `CMAKE_BUILD_TYPE=Debug`; в этом проекте также включается `DEBUG` |
| `make clean` | `cmake --build build-release --target clean` |
| `make install` | `cmake --install build-release` после компиляции |
| `make install DESTDIR=...` | `DESTDIR=... cmake --install build-release` |
| `make apidocs` | Включить `YATE_BUILD_DOCUMENTATION=ON`, затем собрать цель `apidocs` |

**CTest не компилирует проект.** Перед ним всегда выполняйте `cmake --build`. При CMake 3.16 запускайте CTest из каталога сборки, как в примерах выше; форма `ctest --test-dir ...` используется более новыми версиями и текущим CI.

Имена целей плагинов задаются без `.yate` и без каталога: `pgsqldb`, а не `server/pgsqldb.yate`. Доступные цели Make/Ninja можно посмотреть командой:

```sh
cmake --build build-release --target help
```

## 5. Параметры сборки

### 5.1 Общие параметры

| Параметр | Значение по умолчанию при первом запуске | Назначение |
| --- | --- | --- |
| `CMAKE_BUILD_TYPE` | `RelWithDebInfo` для одноконфигурационной сборки | Режим оптимизации и отладочной информации |
| `BUILD_TESTING` | `ON` | Регистрация CTest и сборка теста ядра |
| `YATE_BUILD_MODULES` | `ON` | Включение каталога рабочих и тестовых плагинов |
| `YATE_BUILD_TEST_MODULES` | Первоначальное значение `BUILD_TESTING` | Четыре исходных тестовых плагина и утилита SS7 |
| `YATE_MODULES` | `all` | `all`, `none` либо список имён через `;` |
| `YATE_BUILD_DOCUMENTATION` | `OFF` | Генерация и установка Doxygen HTML |
| `YATE_RELEASE` | `1` | Номер релиза пакета |
| `YATE_STATUS` | `devel` | Статус версии |

Значения `option(...)` остаются в кэше. Если сначала настроить `BUILD_TESTING=ON`, а затем переключить его на `OFF`, уже сохранённое `YATE_BUILD_TEST_MODULES=ON` автоматически не изменится. Для сборки без тестовых артефактов задайте оба параметра явно.

`YATE_BUILD_MODULES=OFF` вместе с `YATE_BUILD_TEST_MODULES=OFF` исключает и каталог внутренних библиотек из общей сборки. Если оставить тестовые модули включёнными, внутренние библиотеки и SS7-утилита могут по-прежнему собираться; сами тестовые `.yate` находятся в каталоге `modules`.

`YATE_MODULES=none` отключает рабочие плагины, но не равнозначен двум флагам `OFF`: SDK-библиотеки и тестовые цели могут оставаться в сборке.

### 5.2 Платформенные параметры

| Параметр | Значение по умолчанию | Назначение |
| --- | --- | --- |
| `YATE_STATIC_STRINGS` | `ON` | Разрешить статические выражения `String`, если компилятор их поддерживает |
| `YATE_ATOMICS` | `ON` | Использовать проверенные атомарные примитивы компилятора |
| `YATE_POLL` | `ON` | Использовать `poll()`, если доступен |
| `YATE_FDSIZE` | `8192` | Переопределение размера `fd_set` на поддерживаемой glibc; `0` отключает |
| `YATE_INTERNAL_REGEX` | `OFF` на Linux, `ON` на остальных Unix | Встроенная реализация регулярных выражений |
| `YATE_RTTI` | `AUTO` | `AUTO`, `ON`, `OFF`; `AUTO` оставляет настройку компилятора |
| `YATE_SSE2` | `OFF` | SSE2 в miniwebrtc; зависит от целевой архитектуры |
| `YATE_ISAC_FIXED` | `OFF` | Фиксированная реализация iSAC вместо плавающей, если iSAC включён |

Значения `ON` для atomics, static strings и poll разрешают использовать возможность после проверки; это не те же строгие требования, что `YATE_WITH_<ИМЯ>=ON` для зависимостей.

Не задавайте внутренние результаты проб, например `YATE_HAVE_*`, вместо пользовательских параметров. Они вычисляются системой сборки.

### 5.3 Перевод опций configure

| Autoconf | CMake |
| --- | --- |
| `--prefix=/opt/yate` | `-DCMAKE_INSTALL_PREFIX=/opt/yate` |
| `--sysconfdir=/etc` | `-DCMAKE_INSTALL_SYSCONFDIR=/etc` |
| `--libdir=/usr/lib64` | Обычно `-DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib64` |
| `--with-archlib=...` | Выбрать `CMAKE_INSTALL_LIBDIR`; прямой отдельной опции `archlib` нет |
| `--enable-strings` / `--disable-strings` | `-DYATE_STATIC_STRINGS=ON/OFF` |
| `--enable-atomics` / `--disable-atomics` | `-DYATE_ATOMICS=ON/OFF` |
| `--enable-poll` / `--disable-poll` | `-DYATE_POLL=ON/OFF` |
| `--with-fdsize=N` / `--without-fdsize` | `-DYATE_FDSIZE=N` / `-DYATE_FDSIZE=0` |
| `--enable-internalregex` | `-DYATE_INTERNAL_REGEX=ON` |
| `--enable-rtti=yes/no/auto` | `-DYATE_RTTI=ON/OFF/AUTO` |
| `--enable-sse2` | `-DYATE_SSE2=ON` |
| `--enable-isac-fixed` | `-DYATE_WITH_ISAC=ON -DYATE_ISAC_FIXED=ON` |
| `--disable-isac-float` без включения fixed | `-DYATE_WITH_ISAC=OFF` |
| `--with-status=...` | `-DYATE_STATUS=...` |
| `--with-doxygen=...` | `-DYATE_BUILD_DOCUMENTATION=ON`; путь задаётся средствами FindDoxygen |

Опции `--enable-inline`, `--enable-mallinfo`, `--enable-lib-exec` и KDoc не имеют отдельных пользовательских аналогов в текущем CMake. `mallinfo` определяется пробой; права установки и compiler flags задаются правилами CMake. Не переносите старую командную строку механической заменой `--` на `-D`.

## 6. Зависимости и состав модулей

### 6.1 Режимы AUTO, ON и OFF

Каждый параметр `YATE_WITH_<ИМЯ>` принимает три значения:

| Значение | Поведение |
| --- | --- |
| `AUTO` | Использовать доступную зависимость; зависимый модуль может быть пропущен |
| `ON` | Требовать зависимость; отсутствие или обнаруженная несовместимость — ошибка настройки |
| `OFF` | Не включать соответствующую возможность или зависимые модули |

Для обязательных компонентов продукта задавайте `ON`. Для исследовательской сборки удобен `AUTO`. Набор устанавливаемых пакетов и конфигурацию продукта фиксируйте вместе.

### 6.2 Имена зависимостей

В таблице указан суффикс параметра: например, `OPENSSL` означает `YATE_WITH_OPENSSL`.

| Суффикс | Основная возможность или модуль | Старый параметр |
| --- | --- | --- |
| `OPENSSL` | `openssl` | `--with-openssl` |
| `ZLIB` | `zlibcompress` | `--with-zlib` |
| `POSTGRESQL` | `pgsqldb` | `--with-libpq` |
| `MYSQL` | `mysqldb`, MySQL либо MariaDB connector | `--with-mysql` |
| `SQLITE` | `sqlitedb` | `--with-sqlite` |
| `RESOLVER` | DNS resolver и `enumroute` | `--enable-resolver` |
| `SCTP` | `lksctp`, полная зависимость от заголовков и библиотеки | `--enable-sctp` |
| `GSM` | `gsmcodec` | `--with-libgsm` |
| `SPEEX` | `speexcodec` | `--with-libspeex` |
| `AMRNB` | `amrnbcodec`, `efrcodec`, совместимый старый API | `--with-amrnb` |
| `SPANDSP` | `faxchan` | `--with-spandsp` |
| `LIBUSB` | `ybladerf` | `--with-libusb` |
| `OSS` | `osschan` | Автоматическая проверка заголовков |
| `ALSA` | `alsachan` | Автоматическая проверка ALSA |
| `COREAUDIO` | `coreaudio` на macOS | Автоматическая проверка frameworks |
| `DAHDI` | `zapcard` через DAHDI | `--enable-dahdi` |
| `ZAPTEL` | `zapcard` через старые заголовки Zaptel | `--enable-zaptel` |
| `WANPIPE` | `wpcard`; при наличии TDM API также `tdmcard` | `--enable-wanpipe` |
| `WANPIPE_HWEC` | Дополнение Wanpipe для эхокомпенсации | `--with-wphwec` |
| `COREDUMPER` | Дополнение `rmanager` | `--with-coredumper` |
| `ILBC` | Встроенная библиотека и `ilbccodec` | `--enable-ilbc` |
| `ILBC_WEBRTC` | `ilbcwebrtc` | `--enable-ilbc-webrtc` |
| `ISAC` | `isaccodec`; вариант выбирает `YATE_ISAC_FIXED` | `--enable-isac-float/fixed` |
| `G722_WEBRTC` | `g722webrtc` | `--enable-g722-webrtc` |

Базовые SCTP-возможности сокетов ядра и наличие полного модуля `lksctp` — разные результаты проверки. Если продукту нужен `lksctp`, требуйте `YATE_WITH_SCTP=ON` и проверяйте его наличие в списке модулей.

Параметров `YATE_WITH_ZAP`, `YATE_WITH_WPCARD` и `YATE_WITH_TDMCARD` нет. `ZAP` и `TDMCARD` — внутренние производные возможности; отдельные рабочие цели выбираются фильтром модулей.

### 6.3 Выбор модулей

```sh
cmake -S . -B build-sip \
  '-DYATE_MODULES=ysipchan;yrtpchan;regexroute;tonegen' \
  -DYATE_BUILD_TEST_MODULES=OFF
cmake --build build-sip --parallel 4
```

Кавычки вокруг списка обязательны для Bash: иначе `;` разделит команды. В списке используются базовые имена целей, даже для файлов из `server`, `sig` и других подкаталогов.

Неизвестное имя вызывает ошибку. Если явно запрошенный модуль требует недоступную зависимость, настройка также завершится ошибкой. Исходные тестовые плагины не выбираются этим списком: ими управляет `YATE_BUILD_TEST_MODULES`.

Фильтр сокращает набор плагинов, но не гарантирует минимальную сборку всех SDK-библиотек: их цели описаны отдельно. Отключайте ненужные встроенные кодеки собственными `YATE_WITH_*`.

После исключения модуля уже существующий `.yate` остаётся на диске и может быть найден при запуске. Для другого состава используйте новый каталог сборки и отдельный установочный префикс либо чистый staging-каталог пакета.

### 6.4 Зависимости из нестандартного каталога

Сначала укажите префиксы поиска:

```sh
cmake -S . -B build-custom \
  '-DCMAKE_PREFIX_PATH=/opt/yate-deps;/opt/vendor-sdk' \
  -DYATE_WITH_MYSQL=ON
```

Для MySQL/MariaDB предусмотрены конкретные переменные поиска:

```sh
cmake -S . -B build-mariadb \
  -DYATE_WITH_MYSQL=ON \
  -DYATE_MYSQL_INCLUDE_DIR=/opt/mariadb/include/mariadb \
  -DYATE_MYSQL_LIBRARY=/opt/mariadb/lib/libmariadb.so
```

Пути в этом примере заменяются реальными путями установленного SDK. Для SpanDSP используется `pkg-config`; при нестандартной установке также настройте `PKG_CONFIG_PATH`. Переменные поиска конкретной зависимости сверяйте с [Dependencies.cmake](../cmake/Dependencies.cmake) и соответствующим `Find`-модулем CMake.

## 7. Готовые профили

### 7.1 Сервер с обязательными БД и TLS

```sh
cmake -S . -B build-server \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DYATE_WITH_OPENSSL=ON \
  -DYATE_WITH_ZLIB=ON \
  -DYATE_WITH_SQLITE=ON \
  -DYATE_WITH_MYSQL=ON \
  -DYATE_WITH_POSTGRESQL=ON
cmake --build build-server --parallel 4
(cd build-server && ctest --output-on-failure)
```

Этот профиль оставляет остальные модули в режиме автоматического выбора. Для фиксированного продукта дополнительно задайте полный `YATE_MODULES`.

### 7.2 Только ядро и исполняемый файл

```sh
cmake -S . -B build-core \
  -DCMAKE_BUILD_TYPE=Release \
  -DYATE_BUILD_MODULES=OFF \
  -DYATE_BUILD_TEST_MODULES=OFF \
  -DBUILD_TESTING=OFF
cmake --build build-core --parallel 4
```

Для проверки ядра оставьте `BUILD_TESTING=ON`: добавятся тесты справки и базовых классов. Проверки обязательных зависимостей, явно заданных через `YATE_WITH_*=ON`, выполняются и при отключённых модулях.

### 7.3 Debug с альтернативными реализациями

```sh
cmake -S . -B build-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DYATE_INTERNAL_REGEX=ON \
  -DYATE_ATOMICS=OFF \
  -DYATE_STATIC_STRINGS=OFF \
  -DYATE_POLL=OFF \
  -DYATE_RTTI=OFF \
  -DYATE_SSE2=ON \
  -DYATE_ISAC_FIXED=ON
cmake --build build-debug --parallel 4
(cd build-debug && ctest --output-on-failure)
```

Именно сочетание этих альтернатив проверялось на x86_64. Для другой архитектуры не включайте SSE2 без поддержки целевого процессора.

### 7.4 Другой компилятор или cross-compilation

Компилятор выбирайте при создании отдельного build-каталога, например через `CC` и `CXX`. Для cross-compilation используйте `CMAKE_TOOLCHAIN_FILE`, задающий toolchain и sysroot. Проверки CMake не заменяют запуск и приёмку на целевой системе. Назначение toolchain-файла описано в [официальной документации](https://cmake.org/cmake/help/v3.16/variable/CMAKE_TOOLCHAIN_FILE.html).

## 8. Структура сборочной системы

| Файл | За что отвечает |
| --- | --- |
| [CMakeLists.txt](../CMakeLists.txt) | Версия, общие параметры, порядок подключения каталогов, исполняемый файл |
| [cmake/PlatformChecks.cmake](../cmake/PlatformChecks.cmake) | Компилятор, потоки, функции ОС, atomics, poll, regex |
| [cmake/Dependencies.cmake](../cmake/Dependencies.cmake) | Поиск внешних библиотек, проверки их API, производные возможности |
| [cmake/YateHelpers.cmake](../cmake/YateHelpers.cmake) | `yate_module`, `yate_library`, опции зависимостей, RPATH |
| [engine/CMakeLists.txt](../engine/CMakeLists.txt) | Цель `libyate`, публичные определения и платформенные части ядра |
| [libs/CMakeLists.txt](../libs/CMakeLists.txt) | Внутренние библиотеки и их связи |
| [libs/ilbc/CMakeLists.txt](../libs/ilbc/CMakeLists.txt) | Встроенный iLBC |
| [libs/miniwebrtc/CMakeLists.txt](../libs/miniwebrtc/CMakeLists.txt) | Явный список miniwebrtc и SSE2 |
| [modules/CMakeLists.txt](../modules/CMakeLists.txt) | Список рабочих и исходных тестовых плагинов |
| [cmake/InstallYate.cmake](../cmake/InstallYate.cmake) | SDK, ресурсы, конфигурация, документация, вспомогательные скрипты |
| [cmake/InstallConfigs.cmake.in](../cmake/InstallConfigs.cmake.in) | Установка конфигурации без перезаписи существующих файлов |
| [tests/CMakeLists.txt](../tests/CMakeLists.txt) | CTest и условия включения проверок |
| [.github/workflows/cmake.yml](../.github/workflows/cmake.yml) | Linux CI с Release/Debug |

Зависимости выражаются между целями. Например, `ysipchan` ссылается на `libyate`, `yatesip`, `yatesdp`; `yatesig` публично зависит от `yateasn`. Общие принципы целей и транзитивных требований описаны в [cmake-buildsystem](https://cmake.org/cmake/help/v3.16/manual/cmake-buildsystem.7.html).

### 8.1 Имена и типы целей

| Цель | Тип | Результат на проверенном Linux |
| --- | --- | --- |
| `yate` | `EXECUTABLE` | `yate` |
| `libyate` | `SHARED` | `libyate.so.6.4.1` и ссылка `libyate.so` |
| `yatesig` | `SHARED` | `libyatesig.so.6.4.1` |
| `yatesip` | `STATIC` | `libyatesip.a` |
| `ysipchan` | `MODULE` | `modules/ysipchan.yate` |
| `pgsqldb` | `MODULE` | `modules/server/pgsqldb.yate` |

Полная версия `6.4.1` сохранена в `SOVERSION` общих библиотек, чтобы соответствовать исходным именам SONAME. Не меняйте её на один major как косметическую правку: это отдельное изменение совместимости и упаковки.

Файлы C/C++ перечисляются явно. Добавление нового `.cpp` в каталог само по себе не включает его в сборку. `GLOB` используется для некоторых ресурсов и устанавливаемых заголовков, но не заменяет списки компилируемых исходников.

### 8.2 Общие флаги и публичный интерфейс

`yate_build_options` содержит общие параметры компиляции для сборки проекта, включая `-Wall`, `-fno-exceptions` для C++ и `DEBUG` в Debug. Это внутренняя цель; установленный SDK не предоставляет её внешним проектам.

`YATE_PUBLIC_DEFINITIONS` передаются через `libyate` как `PUBLIC`. К ним относятся результаты выбора atomics, статических строк, проверки форматов и endian. Платформенные определения ядра отделены от публичных.

При добавлении настройки определите, где она нужна: только в одном `.cpp`, во всём модуле или также в публичном заголовке. Используйте `target_compile_definitions`, `target_compile_options`, `target_include_directories` и `target_link_libraries` на соответствующей цели. Глобальные строки `CXXFLAGS` и ручные `-I`/`-L` не должны заменять граф зависимостей.

### 8.3 Генерируемые файлы и версия

`yateversn.h` формируется из `yateversn.h.in`, а `yatepaths.h` — из шаблона CMake. Версию меняют в `project(Yate VERSION ...)`; статус и релиз — через `YATE_STATUS` и `YATE_RELEASE`.

Ревизия берётся из Git HEAD, затем из `packing/revision.txt` либо `packing/git_commit.txt`; если источника ревизии нет, используется `unknown`. Незакоммиченные изменения не получают отдельного идентификатора автоматически. Для выпуска привяжите бинарные артефакты к конкретной сохранённой ревизии.

## 9. Добавление модуля

### 9.1 Исходник плагина

Пример `modules/server/my_router.cpp`:

```cpp
#include <yatengine.h>

using namespace TelEngine;

namespace {

class MyRouterPlugin : public Plugin
{
public:
    MyRouterPlugin()
        : Plugin("my_router",true)
    {
    }

    void initialize() override
    {
        // Здесь устанавливаются обработчики сообщений и читается конфигурация.
        Output("Initializing MyRouter");
    }
};

INIT_PLUGIN(MyRouterPlugin);

}
```

Это компилируемый каркас плагина. Он не реализует маршрутизацию вызовов: обработчики сообщений добавляются по требованиям конкретного модуля.

### 9.2 Регистрация цели

В `modules/CMakeLists.txt`, рядом с подходящей группой рабочих модулей, добавьте:

```cmake
# Плагин без дополнительных библиотек.
yate_module(my_router server/my_router.cpp)
```

Если модулю нужен SIP API, замените эту регистрацию следующим вариантом:

```cmake
yate_module(my_router server/my_router.cpp
    LIBRARIES yatesip yatesdp)
```

`libyate`, общие build options, расширение `.yate`, выходной подкаталог и правила установки добавляет helper. Не указывайте `-shared`, префикс `lib` или ручную команду линковки.

Helper принимает один основной исходник. Остальные файлы добавляйте после проверки существования цели:

```cmake
yate_module(my_router server/my_router.cpp)
if(TARGET my_router)
    # Цели может не быть из-за фильтра YATE_MODULES.
    target_sources(my_router PRIVATE server/my_router_rules.cpp)
endif()
```

Без `if(TARGET ...)` настройка с фильтром, исключающим модуль, завершится ошибкой на `target_sources`.

### 9.3 Модуль с внешней зависимостью

Для уже существующей зависимости используйте ту же модель, что `sqlitedb`:

```cmake
yate_module(my_sqlite_module server/my_sqlite_module.cpp
    FEATURE SQLITE
    LIBRARIES SQLite::SQLite3)
```

`FEATURE` использует внутреннее имя возможности, вычисленной в `Dependencies.cmake`. Здесь модуль пропускается при недоступном SQLite в общей сборке; явный запрос такого модуля через `YATE_MODULES` вызывает ошибку.

Не связывайте зависимость проверкой наличия произвольного файла `.so`: нужны заголовки, пригодный API и библиотека для целевого toolchain.

### 9.4 Конфигурация и проверка

Если модуль читает `my_router.conf`, добавьте `conf.d/my_router.conf.sample`. Текущие правила автоматически обнаруживают `.sample`/`.default` и устанавливают их под именем без последнего суффикса. Существующий конфиг в build-каталоге или при установке не обновляется автоматически.

Проверьте модуль в новом каталоге:

```sh
cmake -S . -B build-my-router \
  -DYATE_MODULES=my_router \
  -DYATE_BUILD_TEST_MODULES=OFF
cmake --build build-my-router --target my_router --parallel 4
cmake --build build-my-router --parallel 4
(cd build-my-router && ctest --output-on-failure)
```

Сборка одной цели не создаёт автоматически все исполняемые файлы, нужные CTest; поэтому перед полным запуском тестов выполнена общая сборка.

Для исходного тестового плагина используйте `TEST` в блоке `YATE_BUILD_TEST_MODULES`. Такой файл размещается в `modules/test` и не устанавливается как рабочий модуль. Имя должно отличаться от других CMake-целей во всём проекте.

## 10. Добавление библиотеки и зависимости

### 10.1 Внутренняя библиотека

Создайте каталог `libs/yfoo`, исходник `foo.cpp` и публичный заголовок `yatefoo.h`. В `libs/CMakeLists.txt`:

```cmake
# Общая библиотека Yate API.
yate_library(yatefoo yfoo SHARED
    yfoo/foo.cpp)
target_link_libraries(yatefoo PUBLIC yateasn)
```

Для статической библиотеки вместо `SHARED` указывается `STATIC`. Helper связывает её с `libyate`, задаёт include-пути, установку и экспорт. Для shared-варианта он также задаёт версию и RPATH.

Публичные заголовки helper не устанавливает. Добавьте `yfoo` в соответствующий список `InstallYate.cmake` либо явное правило:

```cmake
install(FILES "${PROJECT_SOURCE_DIR}/libs/yfoo/yatefoo.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/yate")
```

Модуль подключается к новой библиотеке через `LIBRARIES yatefoo`. В полной установленной сборке экспортируемое имя будет `Yate::yatefoo`.

### 10.2 Новая внешняя зависимость

Последовательность изменений:

1. Добавьте пользовательскую опцию `yate_dependency_option` в `Dependencies.cmake`.
2. Найдите библиотеку через `find_package`, `pkg_check_modules` или `find_path`/`find_library`.
3. Если у библиотеки несколько несовместимых API, добавьте проверку необходимого вызова или структуры.
4. Передайте результат в `yate_dependency_result`.
5. Свяжите модуль через импортированную либо интерфейсную цель и его `FEATURE`.
6. Добавьте проверку `AUTO`, `ON` без зависимости и `OFF`; обновите инструкцию и CI.

Ниже показана **схема для условной библиотеки Foo**, а не готовая зависимость проекта. `Foo`, `Foo::Foo` и `foo.h` заменяются реальным API:

```cmake
yate_dependency_option(FOO "Build with Foo")
find_package(Foo QUIET)
yate_dependency_result(FOO Foo_FOUND)
```

```cmake
yate_module(foo_driver server/foo_driver.cpp
    FEATURE FOO
    LIBRARIES Foo::Foo)
```

Проверки с временными include-путями, библиотеками и определениями заключайте между `cmake_push_check_state(RESET)` и `cmake_pop_check_state()`, как в существующих пробах. Иначе параметры одной зависимости могут повлиять на следующую.

### 10.3 PUBLIC и PRIVATE при экспорте SDK

Зависимость, необходимая в публичном заголовке, обычно должна передаваться как `PUBLIC`. Реализацию, не видимую потребителю, связывают как `PRIVATE`. У статических библиотек линковочная зависимость может требоваться потребителю и при `PRIVATE`.

Если экспортированные цели ссылаются на внешнюю цель, например `Foo::Foo`, установленный `YateConfig.cmake` должен обеспечить её обнаружение через `find_dependency(Foo)`. Иначе проект Yate соберётся, но сторонний потребитель установленного SDK получит ошибку. Проверяйте отдельную сборку потребителя после установки.

## 11. Внешние модули и установленный SDK

### 11.1 Подготовка локального SDK

Соберите и установите отдельную полную копию в каталог разработчика:

```sh
sdk_dir="$PWD/_sdk"
cmake -S . -B build-sdk \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  "-DCMAKE_INSTALL_PREFIX=$sdk_dir" \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DBUILD_TESTING=OFF \
  -DYATE_BUILD_TEST_MODULES=OFF
cmake --build build-sdk --parallel 4
cmake --install build-sdk
```

Не включайте `_sdk` в исходный пакет или коммит. Профиль только ядра не предоставляет набор библиотечных целей полного SDK.

### 11.2 CMakeLists внешнего проекта

Следующий `CMakeLists.txt` использует установленный экспорт. Скопируйте каркас из раздела 9 в файл `my_router.cpp` рядом с ним:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyRouter LANGUAGES CXX)

find_package(Yate CONFIG REQUIRED)

add_library(my_router MODULE my_router.cpp)
target_compile_features(my_router PRIVATE cxx_std_11)
target_compile_options(my_router PRIVATE -fno-exceptions)
target_link_libraries(my_router PRIVATE Yate::yate)
set_target_properties(my_router PROPERTIES
    PREFIX ""
    SUFFIX ".yate")
```

Из корня внешнего проекта, заменив путь на фактический SDK:

```sh
cmake -S . -B build-plugin -DCMAKE_PREFIX_PATH=/absolute/path/to/_sdk
cmake --build build-plugin --parallel 4
```

`Yate::yate` передаёт include-пути, публичные определения и экспортированный режим RTTI `OFF`, если он выбран для ядра. `MODULE` обеспечивает сборку плагина; необходимые локальные правила, например запрет исключений, внешний проект задаёт явно.

Внутренний helper `yate_module` не устанавливается в SDK. Во внешнем проекте используются обычные команды CMake. Чтобы подключить библиотечный API полной сборки, добавьте нужную цель: например, `Yate::yatesig` или `Yate::yatesip`.

Плагин должен быть собран для той же архитектуры и совместимого ABI. При смене toolchain или публичных параметров ядра пересоберите его. Для запуска добавьте каталог плагина через `-x` и используйте отдельную тестовую конфигурацию.

### 11.3 pkg-config и yate-config

Проверка установленной метаинформации:

```sh
sdk_dir=/absolute/path/to/_sdk
sdk_libdir="$("$sdk_dir/bin/yate-config" --libdir)"
PKG_CONFIG_PATH="$sdk_libdir/pkgconfig" pkg-config --modversion yate
PKG_CONFIG_PATH="$sdk_libdir/pkgconfig" pkg-config --cflags --libs yate
"$sdk_dir/bin/yate-config" --version --modules --param=HAVE_SQLITE
```

`yate.pc` описывает библиотеку ядра. При ручной сборке плагина тип shared/module задаётся отдельно. Для путей с пробелами не разбирайте вывод флагов простым разбиением по пробелу: используйте нормальное shell-совместимое разбиение в инструменте сборки либо импортированные CMake-цели.

В новом `yate-config` сохранены запросы версии, релиза, ревизии, флагов, полных путей и распространённых `--param=HAVE_*`. Полного экспорта всех переменных прежнего Autoconf нет. Точный список находится в [InstallYate.cmake](../cmake/InstallYate.cmake) и [шаблоне helper](../cmake/yate-config.in).

Запрос `--skins` и Qt4/H.323-параметры не поддерживаются. `--ld-strip` возвращает пустую строку. Для stripping установленного бинарного файла используйте отдельный этап упаковки либо `cmake --install ... --strip`. Полные пути берите из `--libdir` и `--modules`, не конструируйте их из `--archlib`.

## 12. Установка и конфигурация

### 12.1 Каталоги установки

| Параметр | Значение при первом конфигурировании |
| --- | --- |
| `CMAKE_INSTALL_PREFIX` | Стандартный префикс CMake, обычно `/usr/local` |
| `CMAKE_INSTALL_BINDIR` | Каталог исполняемых файлов |
| `CMAKE_INSTALL_LIBDIR` | `lib`, `lib64` или multiarch-каталог, в зависимости от платформы |
| `CMAKE_INSTALL_INCLUDEDIR` | Каталог заголовков; Yate добавляет `/yate` |
| `CMAKE_INSTALL_DOCDIR` | Каталог документации, по умолчанию для проекта `Yate` |
| `YATE_INSTALL_MODULEDIR` | `${CMAKE_INSTALL_LIBDIR}/yate` |
| `YATE_INSTALL_CONFIGDIR` | `${CMAKE_INSTALL_SYSCONFDIR}/yate` |
| `YATE_INSTALL_SHAREDIR` | `${CMAKE_INSTALL_DATADIR}/yate` |

Стандартные переменные предоставляет [GNUInstallDirs](https://cmake.org/cmake/help/v3.16/module/GNUInstallDirs.html). Полные runtime-пути Yate вычисляет своей функцией при настройке. Для системного размещения `/usr` и `/etc` задайте `/etc` явно.

Значения `YATE_INSTALL_*` также кэшируются. Если изменить только `CMAKE_INSTALL_LIBDIR` в старом build-каталоге, ранее сохранённый `YATE_INSTALL_MODULEDIR` может остаться прежним. Для нового layout используйте новый build-каталог либо задайте все зависимые пути явно.

### 12.2 Установка в системный layout через staging

```sh
stage_dir="$PWD/stage-root"
cmake -S . -B build-package \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_SYSCONFDIR=/etc \
  -DCMAKE_INSTALL_LIBDIR=lib64 \
  -DYATE_INSTALL_MODULEDIR=lib64/yate \
  -DYATE_INSTALL_CONFIGDIR=/etc/yate \
  -DYATE_INSTALL_SHAREDIR=share/yate \
  -DBUILD_TESTING=OFF \
  -DYATE_BUILD_TEST_MODULES=OFF
cmake --build build-package --parallel 4
DESTDIR="$stage_dir" cmake --install build-package
```

Получатся, в частности, `stage-root/usr/bin/yate`, `stage-root/usr/lib64/yate` и `stage-root/etc/yate`. `DESTDIR` добавляет временный корень и не меняет встроенные конечные пути `/usr` и `/etc`.

Не заменяйте это одной командой `cmake --install --prefix ...`, если должны измениться runtime-пути: `yatepaths.h`, `yate-config` и часть метаинформации уже созданы на этапе настройки.

Для проверки staged-копии передавайте `-c`, `-m`, `-e` с путями staging-каталогов. Используйте отдельные SIP/IAX/Remote Manager-порты, чтобы тестовая копия не занимала адреса работающей системы.

### 12.3 Конфигурационные файлы

На этапе настройки `.sample` и `.default` копируются в build-конфигурацию под именами `.conf`, если такого файла ещё нет. Повторный запуск CMake не обновляет уже существующие файлы автоматически.

При установке существующие конфигурационные файлы и символические ссылки сохраняются. Поэтому новые параметры из обновлённого шаблона нужно объединять с рабочими настройками отдельно. Не используйте перезапись всей конфигурации как способ обновить её схему.

Скрипт `build/run` использует `build/conf.d`, `build/modules` и ресурсы исходного дерева. После переноса или удаления исходников рассчитывать на этот development-скрипт нельзя; установленная копия использует собственные каталоги.

### 12.4 RPATH и удаление установки

На Linux устанавливаемые библиотеки и плагины получают относительный RPATH через `$ORIGIN`; на macOS предусмотрен `@loader_path`. Вложенный плагин и плагин из корня каталога имеют разную глубину пути. Её рассчитывает `yate_install_rpath`.

Для обычной проверки установленной копии не задавайте автоматически `LD_LIBRARY_PATH`: сначала убедитесь, что работает собственный RPATH и загружаются нужные библиотеки.

Цель `uninstall` и конфигурация CPack сейчас не добавлены. Для поставки используйте пакетный менеджер и согласованный список файлов. `install_manifest.txt` полезен для учёта, но не является готовой безопасной процедурой удаления пользовательских настроек.

## 13. Тестирование и отладка

### 13.1 Состав CTest

```sh
cmake --build build-release --parallel 4
(cd build-release && ctest -N)
(cd build-release && ctest --output-on-failure)
```

| Тест | Что проверяет | Когда доступен |
| --- | --- | --- |
| `yate-help` | Запуск исполняемого файла | `BUILD_TESTING=ON` |
| `yate-core` | Строки, regex, DataBlock, конкурирующие POSIX mutex | `BUILD_TESTING=ON` |
| `yate-ss7` | Исходную SS7-утилиту и её завершение без платы | Собрана `yate-ss7test` |
| `yate-load-modules` | Штатные загрузку, инициализацию и выгрузку всех найденных `.yate` | Python 3.6+ и включён каталог модулей |
| `yate-sip-rtp` | OPTIONS, INVITE/ACK, тон PCMU по RTP и BYE | Python и цели `ysipchan`, `yrtpchan`, `regexroute`, `tonegen` |

Отсутствие Python или нужных модулей сокращает список тестов. `100% tests passed` для двух тестов не эквивалентно прохождению полной пятёрки. Сначала проверьте `ctest -N` и требуемый состав целей.

Количество файлов `.yate` не обязано совпадать с количеством зарегистрированных объектов `Plugin`: один файл может создавать несколько объектов, например дополнительный `rex_debug`.

Интеграционные тесты используют loopback и отдельные временные конфигурации. Они не выполняют бизнес-приёмку подключений к внешним БД, аппаратных линий или всех сигнализационных протоколов.

### 13.2 Повторение одной проверки

```sh
(cd build-release && ctest -R '^yate-sip-rtp$' --output-on-failure)
(cd build-release && ctest -R '^yate-ss7$' --output-on-failure)
cmake --build build-release --verbose
```

Если тест жизненного цикла модулей не проходит, он сохраняет диагностический `module-lifecycle.log` рядом с каталогом модулей. Общие результаты CTest находятся в `Testing/Temporary` внутри build-каталога.

### 13.3 Отладчик и IDE

```sh
gdb --args ./build-debug/yate \
  -c "$PWD/build-debug/conf.d" \
  -m "$PWD/build-debug/modules" \
  -e "$PWD/share" -vvv
```

Новый `run` — короткий запуск настроенного бинарного файла. Старые служебные режимы `run --gdb`, `--there`, `--executable`, `--valgrind` в нём не реализованы; соответствующие инструменты запускаются напрямую.

Для IDE используйте корневой `CMakeLists.txt` и выбранный build-каталог. Для Make/Ninja создаётся `compile_commands.json`; не подменяйте его старыми include-путями из `configure`.

### 13.4 Проверка установленного SDK и символов

После изменения установки или экспортов соберите внешний проект из раздела 11 против установленной копии. Не передавайте ему include-пути исходного дерева: иначе отсутствие установленного заголовка останется незамеченным.

На Linux сравнивайте наборы `.yate`, зависимости через `ldd -r`, SONAME/RUNPATH через `readelf -d` и экспортируемые символы через `nm -D --defined-only`. Различайте публичные символы API и слабые/шаблонные символы компилятора. Сохранение имён функций дополняет, но не заменяет ABI- и runtime-приёмку.

## 14. Перенос изменений из своего форка

### 14.1 Зафиксировать состав кандидата

Начните с проверки состояния и новых файлов:

```sh
git status --short
git diff --stat
git ls-files --others --exclude-standard
```

Новые `CMakeLists.txt`, шаблоны и тесты должны войти в версионируемую ревизию. Архив Git HEAD не содержит незакоммиченные изменения и неотслеживаемые файлы. Базовый коммит `6c9170e0` не содержит миграцию: исходный пакет нужно создавать из ревизии ветки, в которую уже включены CMake-файлы и тесты.

Сравните свой форк с исходной базой и составьте список собственных модулей, библиотек, флагов и ресурсов. При наличии ripgrep полезен такой аудит:

```sh
rg -n 'PROGS|OBJS|LOCALFLAGS|EXTERNFLAGS|LOCALLIBS|EXTERNLIBS' \
  Makefile.in engine/Makefile.in modules/Makefile.in libs
rg -n 'AC_ARG_|AC_CHECK_|AC_TRY_|AC_SUBST' configure.ac
rg --files -g 'YateLocal*'
```

Локальные `YateLocal.ac` и `YateLocal.mak` CMake автоматически не читает. Пользовательские изменения из них переносите в параметры, цели и probes CMake либо в отдельный явно подключённый CMake-файл.

### 14.2 Разделить исходную сборку и кандидата

Подготовьте две отдельные копии дерева: исходный вариант и CMake-кандидат. Выполните базовую сборку Autoconf в своей копии, затем CMake — в отдельном build-каталоге кандидата. Старая проверка `configure` запрещает некоторые символы, включая пробелы в пути; для baseline используйте подходящий временный путь Linux.

Не используйте общие объектные файлы, сгенерированные заголовки и install-префиксы. Иначе успешная линковка может зависеть от артефакта предыдущей системы сборки.

### 14.3 Переносить по графу зависимостей

| Этап | Что переносится | Критерий завершения |
| --- | --- | --- |
| Ядро | Список исходников, проверки ОС/компилятора, версия, пути, общая библиотека | Собираются `libyate` и `yate`; работают тесты ядра |
| Библиотеки | `STATIC`/`SHARED`, include-пути, зависимости, экспорт | Каждая нужная библиотека имеет самостоятельную цель |
| Базовые модули | Маршрутизация, тон, SIP, RTP и собственные простые плагины | Работает локальный проверочный звонок |
| Необязательные модули | БД, TLS, кодеки, аппаратные SDK | Корректны AUTO/ON/OFF и сообщения об ошибках |
| Установка | Каталоги, RPATH, ресурсы, конфиги, SDK | Кандидат работает из установленной копии |
| Поставка | CI, исходный архив, RPM/Gentoo, службы | Пакет собирается и обновляется в чистом окружении |

При переносе каждой Makefile-цели проверьте:

- все исходники, включая дополнительные C/Objective-C++ файлы;
- платформенные и покомпонентные `-D`, а также отличие публичных определений;
- зависимости `-l...`, include-пути и порядок построения библиотек;
- тип итогового файла, имя, расширение, SONAME и каталог установки;
- генерируемые заголовки, скрипты и ресурсы;
- правила обновления конфигурации и конечные runtime-пути.

Зависимость в CMake указывается целью, а не путём к старому `lib*.so` из соседнего дерева. Для новых модулей используйте `yate_module`, для библиотек — `yate_library`; особенности добавляйте адресно на созданную цель.

### 14.4 Сравнить исходную сборку и CMake

Сравнивайте на одинаковых версиях зависимостей и одинаковом toolchain. Отдельно сохраните перечень включённых возможностей, набор `.yate`, устанавливаемые файлы и конфигурацию продукта.

Сначала проверьте ядро и динамические зависимости, затем инициализацию модулей, SIP/RTP и собственные бизнес-сценарии. После этого переходите к реальным БД, платам и нагрузке. Сбой, воспроизводящийся и на baseline, нужно отделять от регрессии миграции, но учитывать в приёмке продукта.

На этой базе таким способом были найдены и исправлены старый путь цели `make test`, обращение к `&YSTRING(...)` при отключённых статических строках и завершение SS7-утилиты с неразорванными связями компонентов.

## 15. Пакеты и завершение миграции

### 15.1 Что уже сделано

CMake самостоятельно конфигурирует, собирает и устанавливает проект, экспортирует SDK и поддерживает staging. Linux workflow описывает Release/Debug, CTest и staged install. После публикации ветки актуальные результаты доступны в [GitHub Actions](https://github.com/miamaster/yate/actions/workflows/cmake.yml?query=branch%3Acodex%2Fcmake-migration). Обе конфигурации проверяются независимо; установленные в CI дополнительные зависимости, включая SpanDSP, требуются явно.

### 15.2 Что ещё требуется пакетным сценариям

[RPM spec](../packing/rpm/yate.spec.in) по-прежнему вызывает `./configure` и `make`; [Gentoo ebuild](../packing/portage/yate.ebuild.in) — `econf`/`emake`. Из них убраны удалённые компоненты, но их build/install-этапы ещё не переведены на CMake.

Для RPM ядро замены `%build` и `%install` выглядит следующим образом. Это фрагмент будущего spec, а не полностью готовое описание пакета:

```spec
%build
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_SYSCONFDIR=%{_sysconfdir} \
    -DCMAKE_INSTALL_LIBDIR=%{_lib} \
    -DCMAKE_INSTALL_DOCDIR=%{_datadir}/doc/yate-%{version} \
    -DBUILD_TESTING=OFF \
    -DYATE_BUILD_TEST_MODULES=OFF
cmake --build build --parallel 4

%install
DESTDIR=%{buildroot} cmake --install build
```

При реальном переносе необходимо также:

1. Добавить CMake и нужные пакеты разработки в build dependencies; заменить старые `conf_*` и дополнительные configure-аргументы на реальные CMake-параметры.
2. Согласовать lib/include/doc/config-каталоги с политикой дистрибутива. В CMake устанавливаются также статические архивы SDK — учтите `*.a` в devel-пакете.
3. Проверить владение файлами и symlink, конфигурацию `%config(noreplace)`, лицензии, ресурсы и описание devel-пакета.
4. Если сохраняется API-документация в `%files`, включить `YATE_BUILD_DOCUMENTATION=ON` и обеспечить Doxygen; иначе изменить список документов пакета.
5. Установить service/init-файлы отдельным правилом пакета: текущий CMake не устанавливает системную службу автоматически.
6. Проверить stripping, debug info и зависимости пакетов; не переносить старые операции `MODULE_SYMBOLS` без оценки.
7. Собрать пакет из чистого исходного архива и проверить установку, обновление и удаление в тестовой системе.

Аналогично для Gentoo меняются configure/build/install-этапы и mapping USE-флагов. Выбор eclass и обновление устаревшего EAPI требуют отдельной проверки на Gentoo.

### 15.3 Когда можно удалить Autoconf

Отказ от Autoconf — отдельный этап после функционального переноса. Условия:

- все поддерживаемые продуктом профили описаны и проверены в CMake;
- перенесены собственные `YateLocal*`, CI, release-скрипты и пакетные сценарии;
- проверены исходный архив, installed SDK и внешние модули;
- подтверждены layout, конфигурация, запуск службы и обновление установленного продукта;
- README и документация не направляют к удаляемым командам;
- выполнены приёмка на поддерживаемых ОС/архитектурах и тесты оборудования, которое входит в продукт.

После выполнения этих условий удаляются `configure.ac`, `autogen.sh`, устаревшие `Makefile.in` и связанные только с ними генераторы. Сначала найдите всех потребителей каждого файла. Шаблоны вроде `yateversn.h.in`, которые используются и CMake, сохраняются.

Не удаляйте старые файлы только потому, что одна конфигурация Linux успешно скомпилировалась: текущая пакетная сборка пока ещё от них зависит.

## 16. Диагностика

| Симптом | Проверка и действие |
| --- | --- |
| `This fork supports Unix systems only` | Запущен Windows toolchain; выполнять сборку в Linux/WSL либо поддерживаемой Unix-среде |
| Ошибка out-of-source | Задать разные `-S` и `-B` |
| Зависимость установлена, модуль всё равно пропущен | Проверить devel-пакет, нужный API, sysroot и кэш; повторить в новом build-каталоге |
| `YATE_WITH_...=ON but ... not available` | Посмотреть результаты поиска и проб; `ON` намеренно запрещает незаметное исключение зависимости |
| `Unknown Yate module` | Использовать имя цели без каталога и `.yate`; убедиться, что цель зарегистрирована в манифесте |
| `.yate` остался после отключения | Старый файл не удаляется фильтром; использовать новый build-каталог и staging |
| Не находится `YateConfig.cmake` | Сначала установить SDK; передать его префикс в `CMAKE_PREFIX_PATH` |
| Ошибка поиска экспортированной внешней цели | Проверить `find_dependency` и зависимости экспортируемого SDK |
| Плагин собрался, но не загружается | Сверить архитектуру, версии библиотек, `ldd -r`, RUNPATH и публичные параметры ядра |
| Не обновился `.conf` | Существующий файл сохраняется; вручную объединить изменения шаблона или создать чистую тестовую конфигурацию |
| Новый каталог `lib`, а модули всё ещё устанавливаются в старый | Проверить кэш `YATE_INSTALL_MODULEDIR` и остальные производные пути |
| `Unknown option` у `yate-config` | Проверить поддерживаемый поднабор helper; не рассчитывать на все переменные Autoconf |
| CTest показывает меньше пяти тестов | Проверить `BUILD_TESTING`, `YATE_BUILD_TEST_MODULES`, Python и выбранные SIP/RTP-цели |
| Ошибка теста после сборки только одной цели | Выполнить общую сборку перед CTest |
| После переноса build-каталога перестал работать `run` | Генератор сохраняет пути настройки; заново настроить и собрать в конечном build-каталоге |

Просмотр кэша и подробной команды компилятора:

```sh
cmake -LAH -N build-release
cmake --build build-release --verbose
```

Логи проб находятся в `build-release/CMakeFiles`; конкретные имена файлов зависят от версии CMake. Не включайте возможность вручную только для обхода неудачной compile/link-пробы: сначала выясните причину.

## 17. Проверка изменений перед слиянием

Для изменения сборки или платформенного кода проверьте следующий набор. Для изменения только документации достаточно проверить ссылки и затронутые примеры; полная пересборка проекта не обязательна.

- Новая Release-сборка с обязательными зависимостями продукта, а не только `AUTO`.
- Debug и затронутые альтернативные ветки: regex, atomics, poll, RTTI, кодеки.
- Явный набор модулей и отключённая необязательная зависимость; понятная ошибка при требовании отсутствующей зависимости.
- Ожидаемый список CTest; успешный запуск проверок после компиляции.
- Динамические зависимости и жизненный цикл плагинов.
- Установка в отдельный префикс, `DESTDIR` и сохранение существующего конфига.
- Внешний потребитель установленного SDK при изменении заголовков, экспортов или библиотек.
- Пакетные и service-сценарии, если изменение их затрагивает.
- Новые файлы отслеживаются и не скрыты `.gitignore`; `git diff --check` проходит.
- Новые комментарии в CMake, C++ и скриптах написаны на русском.

В описание изменения включайте конечное поведение, затронутые профили, выполненные проверки и реальные границы приёмки. Указывайте версии toolchain и зависимостей, когда они влияют на результат.

## 18. Справочные материалы

Основной источник параметров этого форка — его `CMakeLists.txt`, `cmake/*.cmake` и манифесты целей. Для синтаксиса и общих механизмов CMake:

- [Командная строка CMake 3.16](https://cmake.org/cmake/help/v3.16/manual/cmake.1.html) — configure/build/install, генераторы и кэш.
- [Модель целей CMake 3.16](https://cmake.org/cmake/help/v3.16/manual/cmake-buildsystem.7.html) — типы библиотек и передача требований.
- [GNUInstallDirs](https://cmake.org/cmake/help/v3.16/module/GNUInstallDirs.html) — стандартные каталоги установки.
- [CMAKE_TOOLCHAIN_FILE](https://cmake.org/cmake/help/v3.16/variable/CMAKE_TOOLCHAIN_FILE.html) — выбор toolchain для cross-compilation.

Ссылки на версию 3.16 выбраны по нижней заявленной границе проекта. При использовании возможностей более нового CMake согласованно изменяйте минимальную версию и проверяемую матрицу.
