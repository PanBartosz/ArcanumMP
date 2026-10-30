"""Film the implemented native display settings and their real controls."""
import os
from pathlib import Path
exec(open(Path(os.environ['ARCMP_SHOWCASE_ROOT'])/'tools/showcase/stage.py').read().split("G('mainmenu_ui_begin_campaign(16067)')")[0])
G('mainmenu_ui_start(MM_TYPE_OPTIONS)')
G('mainmenu_ui_create_window()')
G('options_ui_start(OPTIONS_UI_TAB_VIDEO,sub_549820(),0)')
if os.environ.get('ARCMP_SHOWCASE_DIRECT_FRAMES'):
 G('showcase_record_open(%s,%s,(void*)%d,%d,%d,%d)'%(string(str(out/'peer-0.bgr')),string(str(out/'peer-0.frames.json')),int(s['pixels']),int(s['w']),int(s['h']),int(s['pitch'])))
refresh();shot('display-options',4)
mark('widescreen-toggle')
for setting,name in [(5,'widescreen-setting'),(6,'ui-size-setting'),(7,'interaction-setting')]:
 control=V('options_ui_controls[%d].id'%setting)
 G('cyclic_ui_handle_button_pressed(cyclic_ui_controls[%d].right_button_handle)'%control)
 refresh();shot(name,3)
mark('complete')
if os.environ.get('ARCMP_SHOWCASE_DIRECT_FRAMES'):G('showcase_record_close()')
