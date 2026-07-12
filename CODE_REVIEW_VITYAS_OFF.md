# Ревью изменений Kmux после форка Konsole

Дата ревью: 2026-07-11
Текущий `HEAD`: `bf1c3497b`
База fork-дельты: `a59e5c5751953e660287ff6bc4a3dfc1dcfea2b4`
Проверенный диапазон: `a59e5c5751953e660287ff6bc4a3dfc1dcfea2b4..bf1c3497b`

## Область ревью

После указанной базы находится 54 коммита, все с автором `vityas-off <15840124+vityas-off@users.noreply.github.com>`. Совокупная дельта: 119 файлов, примерно 5777 добавленных и 2126 удалённых строк.

Проверены:

- модель проектов и project rail;
- владение tab/view/session, переключение, закрытие и перенос вкладок;
- сохранение и cold/KDE restore;
- `Application`, single-instance запуск и CLI;
- DBus, KPart, plugins и install metadata;
- agent status, Codex/Claude hooks и их конфигурация;
- rebrand, profiles, color schemes, key translators и desktop integration;
- новые и затронутые autotests.

Номера строк ниже относятся к `bf1c3497b`.

## Итог

Найдено 7 проблем уровня High и 13 проблем уровня Medium. До пользовательского релиза в первую очередь стоит закрыть High-1 — High-7: среди них есть два use-after-free, нарушение изоляции проектов, неожиданное повторное выполнение команды, потеря сохранённого workspace, удаление данных соседней установки Konsole и порча Codex config.

## High

### High-1. ✅ Исправлено — завершение сессии в фоновом проекте оставляло dangling pointer в MRU

Статус: исправлено. `sessionFinished()` теперь удаляет завершающийся display из MRU независимо от того, активен ли его проект, а смена фокуса по-прежнему выполняется только внутри активного проекта. Добавлен regression test с завершением сессии фонового проекта, deferred delete и последующей MRU-навигацией.

Места: `src/ViewManager.cpp:911-960`, особенно `958-960`; последующее использование — `582-603`, `718-760`.
Коммит: `001a570dd`.

`sessionFinished()` удаляет `TerminalDisplay` из `_terminalDisplayHistory` только когда его container является активным:

```cpp
if (!_sessionMap.empty() && containerForTerminal(view) == activeContainer()) {
    updateTerminalDisplayHistory(view, true);
    focusAnotherTerminal(...);
}
```

Для фонового проекта `view` получает `deleteLater()`, но сырой указатель остаётся в глобальном MRU. Следующий `Ctrl+Tab`, reverse MRU или `Toggle Between Two Tabs` может передать уже освобождённый `TerminalDisplay *` в `containerForTerminal()` / `parentSplitterForDisplay()` и упасть.

Сценарий: создать проекты A и B, оставить B активным, завершить auto-close сессию в A, дождаться deferred delete, нажать `Ctrl+Tab`.

Исправление: удалять `view` из истории всегда; условным должен быть только выбор нового фокуса. Нужен ASan-тест с естественным завершением Session в неактивном проекте и последующей MRU-навигацией.

### High-2. ✅ Исправлено — Session-сигналы безопасны для нескольких views и обрабатываются один раз

Статус: исправлено. View-specific connections теперь ограничены lifetime соответствующего `TerminalDisplay`, а session-level события подключаются к именованным обработчикам `ViewManager` через `Qt::UniqueConnection`. Обработчики находят все уникальные живые project-контейнеры Session, поэтому одно событие учитывается один раз и обновляет каждый контейнер без захвата raw view. Regression test создаёт два views одной Session, проверяет одиночный `PermissionRequest`, удаляет один view через deferred delete и повторно проверяет status/notification события.

Места: `src/ViewManager.cpp:1149-1178`, счётчик решений — `2632-2671`.
Коммиты: `7a619c5bb`, `57a487607`, `acb7af7d9`, `a7024ec07`.

`createController()` вызывается для каждого `TerminalDisplay`, но сигналы долгоживущей `Session` подключаются с receiver/context `this`; лямбды захватывают raw `view`.

Если одна Session имеет два views, удаление одного view не отключает её connections. Следующий `notificationsChanged`, `terminalNotificationReceived` или `projectStatusChanged` использует dangling pointer. Пока оба views живы, одно событие Session обрабатывается дважды: один Codex `PermissionRequest` увеличит `pendingTerminalDecisions` до 2, и одного Enter уже не хватит для снятия `NeedsInput`.

Исправление: view-specific connections должны иметь `view` как context либо использовать `QPointer`; session-level status нужно подключать ровно один раз на Session и затем обновлять все её живые containers. Нужен тест с двумя views одной Session: одно событие должно учитываться один раз, а удаление одного view не должно приводить к UAF.

### High-3. ✅ Исправлено — DBus layout API мог перепривязать TerminalDisplay между проектами в обход project transfer

Статус: исправлено. `moveView()` и `createSplitWithExisting()` теперь принимают только views активного проекта, которому принадлежит target splitter. Добавлен regression test для обоих cross-project DBus layout paths с проверкой неизменности структуры и владельцев views.

Места: `src/ViewManager.cpp:2376-2456`, особенно `2425-2439`; `2528-2538`; последующее падение возможно в `src/widgets/ViewContainer.cpp:661-664`, `818-826`.
Коммит-интеграция: `001a570dd`.

`moveView()` и `createSplitWithExisting()` ищут target splitter только в активном проекте, но source view — глобально через `findTerminalDisplay()`. В результате DBus-вызов может взять view проекта A и вставить его в splitter проекта B, минуя `moveTabToProject()` и переподключение сигналов.

Старый `TabbedViewContainer` остаётся receiver сигналов controller, а новый не получает нужные connections. На activity старый container вычисляет для splitter из B индекс `-1`, после чего `setTabActivity(-1)` разыменовывает `viewSplitterAt(-1)`.

Сценарий: сохранить view ID из A, переключиться в B и вызвать `moveView(viewA, splitterB, 0)`.

Исправление: либо отклонять source вне active container, либо выполнять полный transfer с теми же disconnect/reconnect-инвариантами, что и `moveTabToProject()`. Нужны cross-project DBus tests для `moveView()` и `createSplitWithExisting()`.

### High-4. ✅ Исправлено — завершившаяся одноразовая команда не восстанавливается

Статус: исправлено. Persistence теперь отбрасывает завершившиеся auto-close сессии и пустые после фильтрации splitter/tab entries. При следующем cold start пустой проект получает обычную сессию профиля, а завершившаяся команда не запускается повторно. Regression test использует команду с побочным эффектом в temp-файле и проверяет, что restore не увеличивает счётчик.

Места: `src/ViewManager.cpp:919-925`, `1727-1756`, `1874-1997`; `src/MainWindow.cpp:135`, `834-872`, `934-954`.
Коммиты: `11ec4a220`, `b60b11e53`.

Когда единственная auto-close сессия завершается, `sessionFinished()` эмитит `empty` и возвращается до удаления view. `MainWindow` закрывается и `queryClose()` сохраняет всё ещё присутствующую сессию. В state попадают `Command`, `Arguments` и `AutoClose`; cold restore создаёт профиль с той же командой и вызывает `run()`.

Сценарий: выполнить `kmux -e <команда-с-побочным-эффектом>`. После нормального завершения следующий обычный запуск Kmux неожиданно повторит команду; с `AutoClose=true` это может образовать цикл.

Исправление: не сохранять уже finished/auto-close сессии либо удалить view до закрытия и сохранения. Повторный запуск non-shell команды должен быть только явной opt-in политикой. Нужен integration test с командой, увеличивающей счётчик в temp-файле: после обычного повторного запуска счётчик не должен меняться.

### High-5. ✅ Исправлено — cold start с explicit option сохраняет дерево проектов

Статус: исправлено. Первое окно теперь всегда восстанавливает canonical workspace до обработки явного session request, после чего запрошенная вкладка, layout или tabs file добавляются в активный проект. Повторное сохранение больше не заменяет ранее сохранённое дерево однопроектным состоянием. Regression tests покрывают `--new-tab`, profile options, `--workdir`, hold, background mode, команду, layout и tabs file с двумя предварительно сохранёнными проектами.

Места: `src/Application.cpp:217-226`, `326-353`; `src/MainWindow.cpp:734-749`, `834-954`; `desktop/io.github.kmux_project.kmux.desktop:18-20`.
Коммит: `11ec4a220`.

Если процесса Kmux нет, options `--new-tab`, `--profile`, `-p`, `-e`, `--workdir` и другие запрещают restore. Создаётся новое однопроектное состояние, а при закрытии оно безусловно записывается в ту же группу `LastProjectWorkspaceState`, уничтожая ранее сохранённые проекты, tabs и splits.

Это достижимо штатным desktop action `New Tab` (`Exec=kmux --new-tab`), если action вызван при незапущенном Kmux.

Исправление: при создании первого окна восстанавливать canonical workspace, а explicit request добавлять в активный проект; для действительно ephemeral запусков не перезаписывать canonical state. Нужны тесты cold start с каждым explicit option и предварительно сохранёнными несколькими проектами.

### High-6. ✅ Исправлено — удаление legacy scheme/keytab из Kmux удаляло файл соседнего Konsole

Статус: исправлено. Legacy-ресурсы из `konsole/` остаются доступными для чтения, но delete/reset теперь работают только с файлами в namespace `kmux/`. Редактирование создаёт собственную Kmux-копию, а regression tests для color schemes и keytabs проверяют, что legacy-файл не изменяется и не удаляется.

Места: `src/colorscheme/ColorSchemeManager.cpp:28-48`, `178-189`, `224-248`; `src/keyboardtranslator/KeyboardTranslatorManager.cpp:25-44`, `77-106`.
Коммиты: `7ad0c6909`, `085b37367`.

Fallback намеренно читает `~/.local/share/konsole`, но `is*Deletable()` считает такой путь редактируемым, а `delete*()` вызывает `QFile::remove()` для первого найденного файла.

Сценарий: существует только `~/.local/share/konsole/My.colorscheme` или `My.keytab`. Ресурс виден в Kmux; Delete/Reset физически удаляет файл, которым продолжает пользоваться Konsole.

Исправление: legacy paths должны быть read-only; edit/delete следует делать через copy-on-write в `kmux/`. Нужны tests с отдельными writable roots `konsole/` и `kmux/`, проверяющие, что Kmux никогда не изменяет legacy-файл.

### High-7. ✅ Исправлено — hook installer превращал валидный Codex TOML в невалидный

Статус: исправлено. Редактор теперь распознаёт любой root dotted key `features.*`, quoted-варианты таблицы и ключей, а также inline table; форма `hooks = true` выбирается без повторного объявления уже созданной таблицы. Install стал идемпотентным, uninstall точно восстанавливает исходные строки. Добавлена integration-матрица для dotted, quoted и inline форм, включая nested inline keys и повторный install/uninstall.

Места: `src/konsole-agent-hooks.cpp:464-531`, особенно `503-530`.
Коммит: `acb7af7d9`.

Редактор распознаёт только `[features]` и точный dotted key `features.hooks`. При валидном config:

```toml
features.experimental_mode = true
```

installer добавляет отдельный `[features]`, повторно объявляя уже неявно созданную таблицу. Это воспроизведено на текущем binary: Python `tomllib` завершился с `Cannot declare ('features',) twice`.

Проблема особенно опасна потому, что `kmux-codex` автоматически запускает installer перед каждым Codex.

Исправление: использовать настоящий TOML parser/editor. Узкий fallback — при любом top-level `features.*` добавлять `features.hooks = true`, а не новый header. Нужна матрица tests для dotted keys, quoted tables, inline tables и повторного install/uninstall.

## Medium

### Medium-1. ✅ Исправлено — обычные MRU tab shortcuts остаются в активном проекте

Статус: исправлено. `Last Used Tabs`, reverse MRU и `Toggle Between Two Tabs` теперь фильтруют общую историю terminal displays по активному `TabbedViewContainer`; fallback после закрытия terminal использует ту же project-local историю. Добавлен regression test со специально перемежённой историей вкладок двух проектов для всех трёх actions.

Места: `src/ViewManager.cpp:582-603`, `718-760`.
Коммит-интеграция: `001a570dd`.

`_terminalDisplayHistory` глобален для окна, а `switchToTerminalDisplay()` автоматически активирует project найденного terminal. Поэтому `Ctrl+Tab`, reverse MRU и `Toggle Between Two Tabs` переходят между side projects, хотя для project navigation есть отдельные actions.

Это нарушает инвариант, что обычные Konsole tab actions работают внутри активного проекта. Историю нужно хранить/фильтровать по `TabbedViewContainer`.

### Medium-2. ✅ Исправлено — перенос фоновой вкладки менял active tab исходного проекта

Статус: исправлено. `moveTabToContainer()` теперь сохраняет identity активного splitter исходного проекта при переносе фоновой вкладки; прежний выбор соседней вкладки остаётся только для переноса активной. Integration test расширен сценарием A/B/C с активной C и проверкой после возврата в исходный проект.

Места: `src/widgets/ViewContainer.cpp:249-285`, особенно `269-272`; context menu — `620-632`.
Коммит: `90a041b44`.

После `removeTab(index)` Qt уже сохраняет текущий widget, корректируя индекс. Код затем безусловно выбирает позицию удалённой вкладки.

Сценарий: A/B/C, active=C; через context menu вкладки A переместить её в другой проект. После возврата active будет B, а не C. Текущий move test оставляет в source только одну вкладку и не видит смену identity.

Исправление: запомнить current widget; если переносилась не активная вкладка, восстановить именно его.

### Medium-3. ✅ Исправлено — клик по notification фонового проекта не активировал этот проект

Статус: исправлено. `ViewManager` теперь обрабатывает `TerminalDisplay::activationRequest` через `setCurrentView()`: активирует проект-владелец, нужную вкладку и terminal, а затем по-прежнему передаёт activation token окну. Notification regression test расширен фоновым проектом с несколькими вкладками и проверками project, tab, terminal, current session и token.

Места: `src/terminalDisplay/TerminalDisplay.cpp:2196-2198`; `src/widgets/ViewContainer.cpp:545-555`; `src/ViewManager.cpp:1360`.
Коммит-интеграция: `001a570dd`.

Клик поднимает окно и просит скрытый `TabbedViewContainer` выбрать tab и дать focus, но нигде не вызывает `ProjectWorkspaceContainer::activateProject()`. Окно остаётся на другом проекте.

Исправление: route activation через `ViewManager::setCurrentView()`/project lookup. Существующий notification test проверяет badge, но не `notificationClicked()` и фактический project/focus.

### Medium-4. ✅ Исправлено — cold restore терял флаги пользовательских tab overrides

Статус: исправлено. Cold state теперь сохраняет и восстанавливает typed-флаги пользовательских overrides для title formats, tab color и activity color; старые state-файлы без новых ключей остаются совместимыми. Regression test проверяет сами флаги, оба title format, оба цвета и их устойчивость после повторного применения профиля с конфликтующими значениями.

Места: save `src/ViewManager.cpp:1727-1756`; restore `1919-1947`; guards `src/session/SessionManager.cpp:225-238`; auto color `src/ViewManager.cpp:1216-1239`.
Коммит: `b60b11e53`.

Сохраняются значения title formats, tab color и activity color, но не `isTabTitleSetByUser()`, `isTabColorSetByUser()` и `isTabActivityColorSetByUser()`. После restore немедленные значения выглядят верно, однако последующий profile update или container detection может их перезаписать.

Нужно сохранять и восстанавливать typed override flags. Test должен проверять не только значение сразу после restore, но и его устойчивость после повторного применения profile/container context.

### Medium-5. ✅ Исправлено — Profile `Directory` сохраняется без явного `--workdir`

Статус: исправлено. Application теперь применяет activation cwd только как fallback для профиля с пустым `Directory`; явный `--workdir` по-прежнему имеет приоритет. То же правило используется для tabs-from-file. Regression tests покрывают first launch, activation, явный override и профиль из tabs-файла.

Места: `src/session/SessionManager.cpp:179-180`; `src/Application.cpp:224-226`, `590-599`; tabs file — `431-438`.
Коммит: `657ed4c0a`.

SessionManager сначала применяет `Profile::Directory`, после чего Application безусловно вызывает `setInitialWorkingDirectory()`. При отсутствии `--workdir` helper возвращает activation cwd, а не «не переопределять».

Профиль с `Directory=/srv/project`, запущенный из `$HOME`, стартует в `$HOME`. Нужен отдельный тест сочетаний profile Directory, first launch, activation и явного/неявного `--workdir`.

### Medium-6. ✅ Исправлено — относительные `--layout` и `--tabs-from-file` используют cwd вызывающего процесса

Места: `src/Application.cpp:221-222`, `283-290`, `633-647`.
Коммиты: `4a0c386ab`, `657ed4c0a`.

KDBus activation передаёт cwd вызывающего процесса и сохраняет его в `m_activationWorkingDirectory`, но raw path передаётся в `QFile`/`loadLayout()` без разрешения относительно этого cwd.

Сценарий: Kmux запущен из `$HOME`; из `/work/project` вызвать `kmux --layout layout.json`. Ищется `$HOME/layout.json`, а не `/work/project/layout.json`.

### Medium-7. ✅ Исправлено — environment второго CLI-запуска доходит до новой сессии

Статус: исправлено. Secondary process теперь отправляет args, cwd и полный environment отдельным DBus request после сериализованной регистрации primary; `QLockFile` закрывает startup race без двойной KDBus activation. Environment хранится в `Session` как отдельная базовая среда: profile entries применяются поверх неё, а caller tokens не попадают в scriptable `environment()` и persisted profile state. Regression test проверяет передачу, изоляцию секретов и сброс среды для следующих локальных вкладок; двухпроцессный DBus smoke test подтверждает значение внутри дочернего shell.

Места: `src/main.cpp:228-245`; `src/Application.cpp:633-654`; `src/session/Session.cpp:573-628`.
Коммит: `4a0c386ab`.

Always-Unique routing передаёт primary process только args и cwd. Новая PTY наследует environment первичного процесса, поэтому обновлённые `PATH`, `SSH_AUTH_SOCK`, `VIRTUAL_ENV`, tokens и пользовательские variables второго запуска теряются.

Удалённый upstream `shouldUseNewProcess()` специально сохранял новый процесс при controlling TTY ради environment propagation. Для single-window модели нужен явный безопасный transport environment/session requests.

### Medium-8. ✅ Исправлено — read-only CLI-команды выполняются локально до регистрации DBus

Места: `src/main.cpp:228-245`; `src/Application.cpp:355-378`, `516-545`, `633-654`; потребитель — `completions/kmux.zsh:7-25`.
Коммит: `4a0c386ab`.

Secondary instance передаёт запрос primary раньше `processHelpArgs()`. Эти options отсутствуют в `hasExplicitSessionRequest()`, поэтому primary только показывает окно, а secondary получает пустой stdout. В результате zsh completion не видит profiles/properties, пока Kmux запущен.

Эти read-only CLI operations нужно выполнять локально до регистрации `KDBusService`.

### Medium-9. ✅ Исправлено — DBus well-known name совпадает с публичным application ID

Статус: исправлено. Application metadata теперь задаются централизованно: `organizationDomain=kmux_project.github.io` вместе с `applicationName=kmux` дают `io.github.kmux_project.kmux`, совпадающий с desktop ID и namespace DBus interfaces. Secondary routing использует тот же источник, а runtime guard сверяет фактическое имя `KDBusService`; regression test фиксирует этот инвариант для CI.

Места: `src/main.cpp:173-184`, `228-233`, `275-278`; ожидаемое имя — `src/autotests/DBusTest.cpp:26`.
Коммит: `bdc242588`.

`applicationName=kmux` и `organizationDomain=github.com` по алгоритму `KDBusService` дают `com.github.kmux`, тогда как desktop ID, interface namespace и DBusTest используют `io.github.kmux_project.kmux`.

Это вывод из текущих KAboutData values; runtime DBus test в данном build не собран. Нужно явно согласовать well-known service с desktop ID и добавить его проверку в CI.

### Medium-10. ✅ Исправлено — DBus `sessionList()`/`sessionCount()` включают все проекты окна

Места: `src/ViewManager.cpp:2200-2218`; контраст — `2229-2246`.
Коммиты: `001a570dd`, `a4f4159d5`.

Количество sessions одного DBus Window меняется только от переключения project, хотя процессы не изменились. Клиент не может перечислить hidden sessions, при этом `setCurrentSession(hiddenId)` умеет переключиться на них, если ID известен другим способом.

Для сохранения upstream DBus semantics `sessionList()` должен возвращать все sessions окна; project-scoped API при необходимости следует добавить отдельно.

### Medium-11. ✅ Исправлено — Kmux KPart больше не подменяет системный `konsolepart.so`

Статус: исправлено. Kmux собирает и устанавливает только собственный `kmuxpart` с отдельными plugin ID и metadata. Compatibility target и тест, требовавший discovery через занятое upstream-имя `konsolepart`, удалены; co-install с Konsole больше не создаёт второго владельца того же plugin path.

Места: `src/CMakeLists.txt:447-455`.
Коммит: `e81232ca4`.

Kmux устанавливает свой `konsolepart.so` в тот же `${KDE_INSTALL_PLUGINDIR}/kf6/parts`, что и upstream. В одном prefix это файловый конфликт; в пользовательском prefix alias может незаметно shadow системный KonsolePart для Dolphin/Kate/Yakuake.

Compatibility нельзя обеспечивать установкой второго владельца того же plugin path. Нужна отдельная discovery/alias стратегия без подмены upstream module.

### Medium-12. Uninstall одного agent config home ломает hooks остальных homes

Места: `src/konsole-agent-hooks.cpp:105-109`, `135-138`, `570-604`, `674-717`, `789-832`.
Коммит: `acb7af7d9`.

Scripts общие для всех configs и лежат в одном `$XDG_DATA_HOME/kmux/hooks`, независимо от `--codex-home`/`--claude-home`. Uninstall любого home безусловно удаляет общие scripts.

Подтверждённый сценарий: установить Codex hooks в A и B с общим data home; uninstall A удаляет все 8 scripts, а `B/hooks.json` продолжает ссылаться на них.

Нужны per-home scripts либо безопасная политика владения/refcount; `status` должен проверять существование и executable каждого handler.

### Medium-13. Claude status не очищается после crash и может унаследовать PID другого agent

Места: `src/konsole-agent-hooks.cpp:63-74`, `146-150`; `src/ViewManager.cpp:2632-2671`, `2700-2725`.
Коммиты: `acb7af7d9`, `a7024ec07`.

`--agent-pid` генерируется только для Codex. При crash/SIGKILL Claude остаётся `Running`/`NeedsInput` навсегда, потому что cleanup пропускает PID 0. Кроме того, event с PID 0 наследует `previousStatus.agentProcessId`; Claude после Codex может получить мёртвый Codex PID и быть очищен преждевременно.

Нужно хранить agent identity, не наследовать PID между agents/новым SessionStart и передавать устойчивый Claude PID. Нужны tests abnormal exit и смены agent в одной terminal session.

## Дополнительные замечания

- `src/workspaces/ProjectWorkspaceModel.cpp:18-25` и restore reuse в `src/ViewManager.cpp:2085-2098`: после восстановления непоследовательной нумерации следующий default title может дублироваться. Пример: сохранить единственный `Project 2`, восстановить, добавить проект — получится второй `Project 2`.
- `src/config-konsole.h.cmake:1`, `src/session/SessionManager.cpp:187-198`: Kmux 0.1.0 экспортирует `KONSOLE_VERSION=000100`. Upstream shell integrations могут принять современную Konsole-базу за древнюю. Product version лучше экспортировать отдельно от compatibility version.
- `CMakeLists.txt:77-93`, `src/main.cpp:200-236`: single-workspace invariant целиком зависит от DBus, который по умолчанию выключен на Windows/macOS; несколько процессов будут сохранять один state по принципу last-writer-wins.
- `src/konsole-agent-hooks.cpp:623-655`, `674-717`, `774-832`: read-modify-write двух config-файлов и общих scripts не защищён `QLockFile`; параллельные auto-install/uninstall могут потерять внешнюю правку или оставить частично согласованное состояние.
- `desktop/kmuxrun.desktop:1-12`: новый service menu не сохранил upstream `X-KDE-AuthorizeAction=shell_access`, поэтому Kiosk policy не сможет скрыть shell action.
- `src/profile/ProfileReader.cpp:34-46`: в отличие от schemes/keytabs, legacy Konsole profiles не имеют даже read-only fallback. Это может быть намеренной rebrand-изоляцией, но расходится с заявленной совместимостью profiles и требует явного продуктового решения.
- Комментарий `src/Application.cpp:274` обещает дополнительный default tab для `--tabs-from-file`, но ветка `223` его больше не создаёт.

## Пробелы тестов

Наиболее важные отсутствующие проверки:

1. Естественное async-завершение Session в background project, deferred delete и MRU navigation под ASan.
2. Два views одной Session: ровно одна status transition и безопасное удаление одного view.
3. Cross-project DBus `moveView()`/`createSplitWithExisting()` и проверка signal ownership.
4. Cold start с сохранённым multi-project state для каждого explicit CLI option.
5. Finished `-e` command не должна сохраняться и выполняться повторно.
6. Move фоновой вкладки при как минимум трёх tabs и проверка identity active tab source project.
7. `notificationClicked()` должен активировать правильные project, tab и terminal.
8. Restore user-override flags с последующим profile/container update.
9. Полные tests `kmux-agent-hooks`: валидность TOML/JSON, idempotency, multi-home, concurrent install/uninstall и реальные generated scripts.
10. Runtime DBus test well-known name, полного session list и cross-project API.

## Верификация

- `git diff --check a59e5c5751953e660287ff6bc4a3dfc1dcfea2b4..HEAD` — успешно.
- `cmake --build build -j2` — успешно (incremental build).
- Полный headless прогон с writable test home и `QT_QPA_PLATFORM=offscreen`: 24 из 25 tests прошли.
- Отдельно успешно прошли `ApplicationTest`, `ViewManagerTest`, `ProjectWorkspaceModelTest`, `KeyboardTranslatorTest`, `ColorSchemeManagerTest` и оба `appstreamtest`.
- `TerminalInterfaceTest::testTerminalInterfaceUsingSpy()` стабильно не получил `currentDirectoryChanged` за 5 секунд (`src/autotests/TerminalInterfaceTest.cpp:198`). Проверяемая assertion не менялась в fork-дельте; причинная связь с изменениями не установлена, failure требует отдельного triage.
- TOML finding High-7 воспроизведён текущим `kmux-agent-hooks`; результат проверен стандартным `tomllib`.
- Runtime DBus probe, ASan/UBSan и ручная GUI-проверка в это ревью не входили.

## Рекомендуемый порядок исправлений

1. High-1, High-2 и High-3: устранить memory safety и восстановить жёсткие границы project ownership.
2. High-4 и High-5: определить безопасную lifecycle/persistence политику до дальнейшего развития restore.
3. High-6 и High-7: исключить изменение чужих данных и порчу agent configs.
4. Medium-1 — Medium-4: закрепить project UX и restore semantics тестами.
5. Затем закрыть CLI/DBus/KPart compatibility и multi-home agent lifecycle.
