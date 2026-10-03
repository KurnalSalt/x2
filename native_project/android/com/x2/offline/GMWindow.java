package com.x2.offline;

import android.app.*;
import android.os.*;
import android.content.*;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.*;
import android.widget.*;
import android.text.*;
import org.json.*;
import java.net.*;
import java.io.*;
import java.util.concurrent.*;

/** Native, activity-attached tool window. No overlay permission or WebView. */
public final class GMWindow implements Application.ActivityLifecycleCallbacks {
    private static final Handler main = new Handler(Looper.getMainLooper());
    private static final ExecutorService io = Executors.newSingleThreadExecutor();
    private static GMWindow instance;
    private Activity activity;
    private TextView bubble, status, notice, wallet;
    private WindowManager manager;
    private WindowManager.LayoutParams position;
    private Dialog dialog;
    private LinearLayout body;
    private JSONObject state = new JSONObject();
    private int tab = 8; // 8 = battle page, shown first
    private String kind = "skin";
    private JSONArray catalog = new JSONArray();
    private int selected = 0, catalogPage=0, catalogTotal=0;
    private TextView pageStatus;
    private Runnable catalogReload;
    private int catalogRequest=0;
    private boolean rendererRefreshed;
    private int rendererPolls;
    private TextView selection;
    private EditText amount, search;
    private LinearLayout rows;
    private final int ink = Color.rgb(232,238,246), muted = Color.rgb(150,166,185), accent = Color.rgb(96,220,190);

    public static void start() {
        main.post(new Runnable() {
            public void run() {
                if (instance != null) return;
                try {
                    Application app = (Application)Class.forName("android.app.ActivityThread").getMethod("currentApplication").invoke(null);
                    if (app == null) { main.postDelayed(this, 500); return; }
                    instance = new GMWindow();
                    app.registerActivityLifecycleCallbacks(instance);
                    instance.findCurrent();
                } catch (Exception e) { android.util.Log.e("X2GM", "initialize", e); }
            }
        });
    }
    private void findCurrent() {
        try {
            Object current = Class.forName("com.unity3d.player.UnityPlayer").getField("currentActivity").get(null);
            if (current instanceof Activity) attach((Activity)current);
            else main.postDelayed(() -> findCurrent(), 1000);
        } catch (Exception ignored) { main.postDelayed(() -> findCurrent(), 1000); }
    }
    private int dp(float value) { return (int)(value * activity.getResources().getDisplayMetrics().density + .5f); }
    private GradientDrawable background(int color, float radius) {
        GradientDrawable shape = new GradientDrawable(); shape.setColor(color); shape.setCornerRadius(dp(radius)); return shape;
    }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(activity); view.setText(value); view.setTextSize(size); view.setTextColor(color); view.setGravity(Gravity.CENTER_VERTICAL); return view;
    }
    private Button button(String value, Runnable click) {
        Button view = new Button(activity); view.setText(value); view.setTextSize(12); view.setTextColor(ink); view.setAllCaps(false);
        view.setMinHeight(0); view.setMinimumHeight(0); view.setPadding(dp(12),0,dp(12),0);
        view.setBackground(background(Color.rgb(43,59,77),8)); view.setOnClickListener(v -> click.run()); return view;
    }
    private LinearLayout column() { LinearLayout view = new LinearLayout(activity); view.setOrientation(LinearLayout.VERTICAL); return view; }
    private LinearLayout line() { LinearLayout view = new LinearLayout(activity); view.setOrientation(LinearLayout.HORIZONTAL); view.setGravity(Gravity.CENTER_VERTICAL); return view; }
    private EditText input(String hint, String value, boolean numeric) {
        EditText view = new EditText(activity); view.setSingleLine(true); view.setTextSize(12); view.setTextColor(ink); view.setHintTextColor(muted);
        view.setHint(hint); view.setText(value); view.setPadding(dp(10),0,dp(10),0); view.setBackground(background(Color.rgb(27,39,55),6));
        view.setImeOptions(android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI | android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN);
        if (numeric) view.setInputType(2); return view;
    }
    private void add(LinearLayout parent, View child, int width, int height, float weight) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(width < 0 ? width : dp(width), height < 0 ? height : dp(height), weight);
        params.setMargins(dp(2),dp(2),dp(2),dp(2)); parent.addView(child, params);
    }
    private void attach(Activity next) {
        if (next == activity && bubble != null) return;
        detach(); activity = next;
        if (!next.getClass().getName().toLowerCase().contains("unity") && !next.getClass().getName().contains("X2")) return;
        next.getWindow().getDecorView().post(() -> {
            if (activity != next || next.isFinishing() || bubble != null) return;
            try {
                manager = (WindowManager) next.getSystemService(Context.WINDOW_SERVICE);
                bubble = text("GM",14,Color.rgb(14,36,37)); bubble.setGravity(Gravity.CENTER); bubble.setTypeface(null,Typeface.BOLD);
                bubble.setBackground(background(accent,22)); bubble.setElevation(dp(6));
                position = new WindowManager.LayoutParams(dp(42),dp(42),WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL, android.graphics.PixelFormat.TRANSLUCENT);
                position.token = next.getWindow().getDecorView().getWindowToken(); position.gravity = Gravity.TOP | Gravity.LEFT;
                position.x = next.getPreferences(0).getInt("gm_x",dp(8)); position.y = next.getPreferences(0).getInt("gm_y",dp(70));
                bubble.setOnTouchListener(new View.OnTouchListener() {
                    float x, y; int oldX, oldY; boolean moved;
                    public boolean onTouch(View v, MotionEvent event) {
                        if (event.getAction() == 0) { x=event.getRawX(); y=event.getRawY(); oldX=position.x; oldY=position.y; moved=false; return true; }
                        if (event.getAction() == 2) {
                            float dx=event.getRawX()-x, dy=event.getRawY()-y;
                            if (Math.abs(dx)+Math.abs(dy)>dp(6)) moved=true;
                            android.util.DisplayMetrics metrics = next.getResources().getDisplayMetrics();
                            position.x=Math.max(0,Math.min(metrics.widthPixels-dp(42),oldX+(int)dx));
                            position.y=Math.max(0,Math.min(metrics.heightPixels-dp(42),oldY+(int)dy));
                            manager.updateViewLayout(bubble,position); return true;
                        }
                        if (event.getAction()==1) {
                            next.getPreferences(0).edit().putInt("gm_x",position.x).putInt("gm_y",position.y).apply();
                            if (!moved) open(); return true;
                        }
                        return true;
                    }
                });
                manager.addView(bubble,position);
                android.util.Log.i("X2GM", "native bubble attached to " + next.getClass().getName());
                if ("G8441".equals(Build.MODEL) && Build.VERSION.SDK_INT >= 34 && !rendererRefreshed) {
                    main.postDelayed(() -> waitForRenderer(next), 3000);
                }
            } catch (Exception error) { bubble=null; android.util.Log.e("X2GM","attach",error); }
        });
    }
    private View findUnityView(View view) {
        if ("com.unity3d.player.UnityPlayer".equals(view.getClass().getName())) return view;
        if (view instanceof ViewGroup) {
            ViewGroup group=(ViewGroup)view;
            for(int i=0;i<group.getChildCount();i++) {
                View found=findUnityView(group.getChildAt(i)); if(found!=null) return found;
            }
        }
        return null;
    }
    private SurfaceView findSurface(View view) {
        if(view instanceof SurfaceView) return (SurfaceView)view;
        if(view instanceof ViewGroup) {
            ViewGroup group=(ViewGroup)view;
            for(int i=0;i<group.getChildCount();i++) {
                SurfaceView found=findSurface(group.getChildAt(i)); if(found!=null) return found;
            }
        }
        return null;
    }
    private void waitForRenderer(Activity owner) {
        if(rendererRefreshed || activity!=owner || owner.isFinishing() || rendererPolls++>=120) return;
        request("status",null,value -> {
            if(value.optBoolean("connected")) main.postDelayed(() -> refreshRendererOnce(owner),20000);
            else main.postDelayed(() -> waitForRenderer(owner),3000);
        });
    }
    private void refreshRendererOnce(Activity owner) {
        if(rendererRefreshed || activity!=owner || owner.isFinishing() || !owner.hasWindowFocus()) return;
        View player=findUnityView(owner.getWindow().getDecorView());
        SurfaceView surface=player==null?null:findSurface(player);
        if(surface==null) return;
        // A real background/foreground cycle repairs observed startup corruption.
        // Try a scoped surface refresh; cold-start recovery is not yet verified.
        try {
            player.getClass().getMethod("windowFocusChanged",boolean.class).invoke(player,false);
            player.getClass().getMethod("pause").invoke(player);
            rendererRefreshed=true;
            int visibility=surface.getVisibility();
            surface.setVisibility(View.INVISIBLE);
            main.postDelayed(() -> {
                surface.setVisibility(visibility);
                try {
                    if(activity==owner && !owner.isFinishing() && owner.hasWindowFocus()) {
                        player.getClass().getMethod("resume").invoke(player);
                        player.getClass().getMethod("windowFocusChanged",boolean.class).invoke(player,true);
                        android.util.Log.i("X2GM","XZ1C renderer surface refreshed");
                    }
                } catch(Exception error) {android.util.Log.e("X2GM","renderer resume",error);}
            },250);
        } catch(Exception error) {android.util.Log.e("X2GM","renderer refresh",error);}
    }
    private void detach() {
        if (dialog != null) { dialog.dismiss(); dialog=null; }
        if (bubble != null && manager != null) { try { manager.removeView(bubble); } catch (Exception ignored) {} }
        bubble=null;
    }
    private interface Result { void done(JSONObject value); }
    private void request(String path, String payload, Result callback) {
        final Activity owner = activity;
        io.execute(() -> {
            JSONObject result;
            try {
                HttpURLConnection connection=(HttpURLConnection)new URL("http://127.0.0.1:9999/gm/"+path).openConnection();
                connection.setConnectTimeout(3000); connection.setReadTimeout(6000);
                if (payload != null) {
                    connection.setRequestMethod("POST"); connection.setDoOutput(true); connection.setRequestProperty("Content-Type","application/x-www-form-urlencoded");
                    byte[] bytes=payload.getBytes("UTF-8"); connection.setFixedLengthStreamingMode(bytes.length);
                    try (OutputStream out=connection.getOutputStream()) { out.write(bytes); }
                }
                ByteArrayOutputStream data=new ByteArrayOutputStream();
                try (InputStream in=connection.getInputStream()) { byte[] buf=new byte[8192]; int n; while((n=in.read(buf))!=-1) data.write(buf,0,n); }
                connection.disconnect(); result=new JSONObject(data.toString("UTF-8"));
            } catch (Exception error) {
                result=new JSONObject(); try { result.put("ok",false).put("message","离线服务暂未就绪，请稍后重试"); } catch(Exception ignored){}
                android.util.Log.e("X2GM","request",error);
            }
            final JSONObject completed=result;
            main.post(() -> { if(activity==owner && !owner.isFinishing()) callback.done(completed); });
        });
    }
    private void refresh() {
        request("status",null,value -> {
            state=value;
            if(status!=null) status.setText(value.optBoolean("connected") ? value.optString("name")+"  ·  Lv."+value.optInt("level")+(value.optBoolean("developer")?"  ·  开发者":"  ·  本地账号") : "离线服务已启动 · 登录后可修改资源");
            if(tab==0 && wallet!=null) wallet.setText("金币 "+value.optInt("gold")+"   光辉 "+value.optInt("crystal")+"   体力 "+value.optInt("power"));
            if(tab==6 || tab==8) render();
        });
    }
    private void action(String name, int id, int count) {
        if(notice!=null) notice.setText("正在保存…");
        request("action","action="+name+"&id="+id+"&count="+count,value -> {
            if(notice!=null) notice.setText(value.optString("message"));
            refresh();
            if(value.optBoolean("ok") && tab==1) loadCatalog();
        });
    }
    private int count() { try { return Integer.parseInt(amount.getText().toString()); } catch(Exception ignored) { return -1; } }
    private void open() {
        if(dialog!=null && dialog.isShowing()) return;
        dialog=new Dialog(activity) {
            @Override public boolean dispatchKeyEvent(KeyEvent event) {
                if(event.getKeyCode()==KeyEvent.KEYCODE_BACK) {
                    if(event.getAction()==KeyEvent.ACTION_UP) dismiss();
                    return true;
                }
                return super.dispatchKeyEvent(event);
            }
        }; dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout root=column(); root.setFocusableInTouchMode(true); root.setPadding(dp(13),dp(8),dp(13),dp(8)); root.setBackground(background(Color.rgb(17,27,42),14));
        LinearLayout header=line(); TextView title=text("X2  /  离线 GM  ·  V0.1",17,ink); title.setTypeface(null,Typeface.BOLD);
        add(header,title,0,32,1); add(header,button("收起",() -> dialog.dismiss()),62,30,0); root.addView(header);
        status=text("连接本地服务…",11,muted); add(root,status,-1,24,0);
        LinearLayout tabs=line();
        add(tabs,button("第一页 · 战斗",() -> { tab=8; render(); }),0,32,1);
        add(tabs,button("第二页 · 常规 GM",() -> { if(tab==8) tab=0; render(); }),0,32,1);
        add(tabs,button("功能分类 ▾",() -> {
            PopupMenu choices=new PopupMenu(activity,tabs);
            final String[] names={"资源","资料库","神格养成","装备","邮件 / 批量","进度","测试 / 备份","官方指令"};
            for(int i=0;i<names.length;i++) choices.getMenu().add(0,i,i,names[i]);
            choices.setOnMenuItemClickListener(item -> { tab=item.getItemId(); render(); return true; }); choices.show();
        }),110,32,0);
        root.addView(tabs);
        body=column(); body.setFocusableInTouchMode(true); add(root,body,-1,0,1);
        notice=text("修改会保存到当前账号；窗口外仍是游戏画面。",11,accent); add(root,notice,-1,24,0);
        dialog.setContentView(root); Window window=dialog.getWindow();
        if(window!=null) {
            window.setBackgroundDrawableResource(android.R.color.transparent); window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            WindowManager.LayoutParams p=window.getAttributes(); p.dimAmount=.28f; window.setAttributes(p);
            android.util.DisplayMetrics metrics=activity.getResources().getDisplayMetrics();
            window.setLayout(Math.min(metrics.widthPixels-dp(36),dp(600)),metrics.heightPixels-dp(30));
            window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
        }
        dialog.show(); render(); body.requestFocus(); refresh();
    }
    private void render() {
        body.removeAllViews();
        if(tab==1) { catalogPage=0; renderCatalog(); }
        else {
            ScrollView scroll=new ScrollView(activity); LinearLayout content=column(); scroll.addView(content); add(body,scroll,-1,-1,0);
            if(tab==8) renderBattle(content);
            else if(tab==0) {
                wallet=text("金币 "+state.optInt("gold")+"   光辉 "+state.optInt("crystal")+"   体力 "+state.optInt("power"),12,accent); add(content,wallet,-1,24,0);
                amount=input("增加数量","100000",true); add(content,amount,-1,32,0);
                LinearLayout one=line();
                add(one,button("金币 +",() -> action("gold",0,count())),0,34,1); add(one,button("光辉 +",() -> action("crystal",0,count())),0,34,1); add(one,button("体力 +",() -> action("power",0,count())),0,34,1); content.addView(one);
                LinearLayout two=line(); add(two,button("宝石粉尘 +",() -> action("dust",0,count())),0,34,1); add(two,button("兽魂 +",() -> command(3,""+count(),0,false,false)),0,34,1); add(two,button("神格经验 +",() -> command(8,"1237907",count(),false,false)),0,34,1); content.addView(two);
                add(content,button("外观券 +",() -> command(8,"1237923",count(),false,false)),-1,34,0);
                add(content,text("金币 / 光辉 / 体力支持精确设置：使用官方指令页的“设置”开关。",11,muted),-1,28,0);
            } else if(tab==2) {
                EditText hero=input("神格编号","1003",true), value=input("数量 / 等级增量","10",true); add(content,hero,-1,32,0); add(content,value,-1,32,0);
                LinearLayout first=line();
                add(first,button("解锁神格",() -> command(4,hero.getText().toString(),0,false,false)),0,34,1); add(first,button("提升等级",() -> command(6,hero.getText().toString(),integer(value),false,false)),0,34,1); add(first,button("提升星级",() -> command(7,hero.getText().toString(),integer(value),false,false)),0,34,1); content.addView(first);
                LinearLayout favor=line(); add(favor,button("好感经验 +",() -> command(22,hero.getText().toString(),integer(value),false,false)),0,34,1); add(favor,button("好感等级 +",() -> command(23,hero.getText().toString(),integer(value),false,false)),0,34,1); add(favor,button("清除好感",() -> confirmCommand(24,hero.getText().toString(),0,false,"清除该神格好感度？")),0,34,1); content.addView(favor);
                add(content,button("打开神格资料库",() -> {kind="hero";tab=1;render();}),-1,32,0);
            } else if(tab==3) {
                EditText id=input("兽主原型编号（EquibBase）","",true); add(content,id,-1,32,0);
                add(content,button("打造六星随机词条兽主",() -> command(15,id.getText().toString(),0,false,false)),-1,36,0);
                add(content,button("打开兽主资料库",() -> {kind="equip";tab=1;render();}),-1,34,0);
                add(content,button("领取魂器 / 神格培养材料",() -> {kind="item";tab=1;render();}),-1,34,0);
                add(content,text("穿戴、强化、魂器融合与宝石镶嵌在游戏内完成，GM 负责发放材料和装备。",11,muted),-1,36,0);
            } else if(tab==4) {
                EditText ids=input("道具编号；批量时用英文逗号分隔","1251014",false), qty=input("每种道具数量","3",true); add(content,ids,-1,32,0);add(content,qty,-1,32,0);
                add(content,button("发送道具邮件",() -> command(19,ids.getText().toString(),integer(qty),false,false)),-1,34,0);
                add(content,button("批量直接领取（最多64种）",() -> command(50,ids.getText().toString(),integer(qty),false,false)),-1,34,0);
                add(content,text("邮件在游戏邮箱领取；直接领取会更新当前背包。",11,muted),-1,28,0);
            } else if(tab==5) {
                EditText level=input("账号等级","60",true), exp=input("账号经验数量","1000",true), section=input("已存在的主线关卡编号","",true);
                add(content,level,-1,30,0); add(content,button("设置账号等级",() -> command(16,level.getText().toString(),0,true,false)),-1,32,0);
                add(content,exp,-1,30,0); add(content,button("增加账号经验",() -> command(17,exp.getText().toString(),0,false,false)),-1,32,0);
                add(content,section,-1,30,0); add(content,button("开放该关卡及之前主线",() -> confirmCommand(29,section.getText().toString(),0,false,"修改当前账号的主线完成记录？")),-1,32,0);
            } else if(tab==6) {
                add(content,text("开发者账号与普通账号使用独立存档。账号切换、恢复备份后请重启游戏。",11,muted),-1,36,0);
                if(state.optBoolean("developerAvailable",false)) add(content,button("启用开发者账号",() -> confirmAccount("developer","切换到独立开发者账号？")),-1,32,0);
                if(state.optBoolean("developer")) add(content,button("返回普通账号",() -> confirmAccount("normal","切回普通账号？")),-1,32,0);
                if(state.optBoolean("developerAvailable",false)) add(content,button("隐藏开发者入口",() -> confirmAccount("hide_developer","隐藏测试入口并恢复普通账号？")),-1,32,0);
                add(content,button("重置账号进度（先自动备份）",() -> confirmCommand(13,"0",0,false,"重置当前账号的进度？角色和背包会保留，当前进度先自动备份。")),-1,32,0);
                add(content,button("恢复最近的重置备份",() -> confirmAccount("restore_backup","恢复当前账号最近的进度备份？")),-1,32,0);
                add(content,button("刷新账号状态",() -> refresh()),-1,30,0);
                add(content,button(state.optBoolean("unityRequested")?"关闭实验 Unity 补丁（重启生效）":"开启实验 Unity 补丁（重启生效）",() -> {
                    request("unity","enabled="+(state.optBoolean("unityRequested")?0:1),value -> {notice.setText(value.optString("message"));refresh();});
                }),-1,32,0);
                add(content,text("P1–P11 为实验补丁，默认关闭；影响人物显示时请关闭并重启。",11,muted),-1,36,0);
            } else renderExpert(content);
        }
        body.requestFocus();
    }
    private void renderBattle(LinearLayout content) {
        LinearLayout saves=line();
        add(saves,button("导入存档",() -> chooseSave(false)),0,34,1);
        add(saves,button("导出存档",() -> chooseSave(true)),0,34,1);
        content.addView(saves);
        final int attack=state.optInt("attackPercent",100), speed=state.optInt("speedPercent",100);
        final boolean god=state.optBoolean("god",false);
        add(content,text("当前：攻击 "+fmt(attack)+"   ·   移速 "+fmt(speed)+"   ·   锁血 "+(god?"开启":"关闭"),12,accent),-1,26,0);
        add(content,text("攻击力",12,ink),-1,22,0);
        LinearLayout atk=line(); final int[] atkValues={100,200,500,1000,10000};
        for(final int v:atkValues) add(atk,toggle(fmt(v),v==attack,() -> battle(v,speed,god)),0,34,1);
        content.addView(atk);
        add(content,text("移动速度",12,ink),-1,22,0);
        LinearLayout mv=line(); final int[] speedValues={100,120,150,200,300};
        for(final int v:speedValues) add(mv,toggle(fmt(v),v==speed,() -> battle(attack,v,god)),0,34,1);
        content.addView(mv);
        add(content,text("锁血",12,ink),-1,22,0);
        LinearLayout lock=line();
        add(lock,toggle("开启锁血",god,() -> battle(attack,speed,true)),0,34,1);
        add(lock,toggle("关闭锁血",!god,() -> battle(attack,speed,false)),0,34,1);
        content.addView(lock);
        add(content,button("全部恢复默认",() -> battle(100,100,false)),-1,32,0);
        add(content,text("攻击和移速在下一场战斗生效；锁血拦截己方角色受到的伤害，可在战斗中切换。",11,muted),-1,36,0);
    }
    private String fmt(int percent) { return percent%100==0 ? (percent/100)+" 倍" : (percent/100.0)+" 倍"; }
    private Button toggle(String label, boolean active, Runnable click) {
        Button view=button(label,click);
        if(active) { view.setBackground(background(accent,8)); view.setTextColor(Color.rgb(14,36,37)); }
        return view;
    }
    private void battle(int attack, int speed, boolean god) {
        if(notice!=null) notice.setText("正在保存战斗设置…");
        request("battle","attack="+attack+"&speed="+speed+"&god="+(god?1:0),value -> {
            if(notice!=null) notice.setText(value.optString("message"));
            refresh();
        });
    }
    private int integer(EditText view) {try{return Integer.parseInt(view.getText().toString());}catch(Exception e){return -1;}}
    private void command(int opt,String first,int second,boolean set,boolean confirmed) {
        if(second<0 || first.trim().isEmpty()){notice.setText("请填写有效编号和数量");return;}
        request("command","opt="+opt+"&v0="+first.replace(" ","")+"&v1="+second+"&set="+(set?1:0)+"&confirm="+(confirmed?1:0),value -> {notice.setText(value.optString("message"));refresh();});
    }
    private void confirmCommand(int opt,String first,int second,boolean set,String message) {
        new AlertDialog.Builder(activity).setTitle("确认修改").setMessage(message).setNegativeButton("取消",null).setPositiveButton("执行",(d,w)->command(opt,first,second,set,true)).show();
    }
    private void renderExpert(LinearLayout content) {
        final int[] opts={0,1,2,3,4,6,7,8,13,15,16,17,18,19,22,23,24,29,50};
        String[] names={"0 体力","1 金币","2 光辉","3 兽魂","4 解锁神格","6 神格等级","7 神格星级","8 领取道具","13 重置进度","15 打造兽主","16 账号等级","17 账号经验","18 宝石粉尘","19 道具邮件","22 好感经验","23 好感等级","24 清除好感","29 开放主线","50 批量道具"};
        Spinner select=new Spinner(activity); ArrayAdapter<String> adapter=new ArrayAdapter<>(activity,android.R.layout.simple_spinner_dropdown_item,names);select.setAdapter(adapter);select.setBackground(background(Color.rgb(205,220,228),6));add(content,select,-1,36,0);
        EditText first=input("参数1：数量 / 编号 / 批量编号","1000",false),second=input("参数2：数量 / 增量","1",true);add(content,first,-1,32,0);add(content,second,-1,32,0);
        CheckBox set=new CheckBox(activity);set.setText("设置绝对值（适用于资源和账号等级 / 经验）");set.setTextColor(ink);set.setTextSize(11);add(content,set,-1,28,0);
        add(content,button("执行官方 GM 指令",()->{int opt=opts[select.getSelectedItemPosition()];if(opt==13 || opt==24 || opt==29)confirmCommand(opt,first.getText().toString(),integer(second),set.isChecked(),"执行所选指令并修改当前账号？");else command(opt,first.getText().toString(),integer(second),set.isChecked(),false);}),-1,34,0);
        add(content,text("覆盖离线模块实现的19种官方 GM 指令；未实现的命令会明确返回错误。",11,muted),-1,28,0);
    }
    private void renderCatalog() {
        LinearLayout filters=line();
        final String[] types={"skin","hero","jewel","item","equip"};
        final String[] labels={"皮肤","神格","宝石","道具","兽主"};
        int initial=0;for(int i=0;i<types.length;i++)if(types[i].equals(kind))initial=i;
        Button category=button(labels[initial]+" ▾",()->{});
        category.setOnClickListener(v->{
            PopupMenu picker=new PopupMenu(activity,category);
            for(int i=0;i<labels.length;i++)picker.getMenu().add(0,i,i,labels[i]);
            picker.setOnMenuItemClickListener(item->{int at=item.getItemId();category.setText(labels[at]+" ▾");kind=types[at];selected=0;catalogPage=0;loadCatalog();return true;});picker.show();
        });
        add(filters,category,86,28,0);search=input("搜索名称 / 编号","",false);add(filters,search,0,28,1);body.addView(filters);
        selection=text("点击一项选择",11,accent);
        ScrollView scroll=new ScrollView(activity);rows=column();scroll.addView(rows);add(body,scroll,-1,0,1);
        LinearLayout commands=line();amount=input("数量","3",true);add(commands,amount,55,30,0);
        add(commands,button("领取 / 解锁 / 打造",()->{if(kind.equals("equip"))command(15,""+selected,0,false,false);else action(kind.equals("skin")?"skin":kind.equals("hero")?"hero":"item",selected,count());}),0,30,1);
        add(commands,button("上一页",()->{if(catalogPage>0){catalogPage--;loadCatalog();}}),64,30,0);add(commands,button("下一页",()->{if((catalogPage+1)*40<catalogTotal){catalogPage++;loadCatalog();}}),64,30,0);body.addView(commands);
        pageStatus=text("读取资料…",10,muted);add(body,pageStatus,-1,18,0);
        search.addTextChangedListener(new TextWatcher(){public void beforeTextChanged(CharSequence s,int a,int b,int c){}public void afterTextChanged(Editable e){}public void onTextChanged(CharSequence s,int a,int b,int c){if(catalogReload!=null)main.removeCallbacks(catalogReload);catalogReload=()->{catalogPage=0;loadCatalog();};main.postDelayed(catalogReload,300);}});loadCatalog();
    }
    private void confirmAccount(String action, String message) {
        new AlertDialog.Builder(activity).setTitle("测试账号").setMessage(message+"\n保存后请退出并重新打开游戏。")
            .setNegativeButton("取消",null).setPositiveButton("保存",(d,w) -> action(action,0,0)).show();
    }
    private void loadCatalog() {
        final String requested=kind; final int sequence=++catalogRequest;
        String query="";try{query=java.net.URLEncoder.encode(search.getText().toString(),"UTF-8");}catch(Exception ignored){}
        request("catalog","kind="+kind+"&page="+catalogPage+"&q="+query,value -> {
            if(tab!=1 || !kind.equals(requested) || sequence!=catalogRequest) return;
            catalog=value.optJSONArray("entries");if(catalog==null)catalog=new JSONArray();catalogTotal=value.optInt("total");pageStatus.setText("第 "+(catalogPage+1)+" / "+Math.max(1,(catalogTotal+39)/40)+" 页 · "+catalogTotal+" 项");showCatalog();
        });
    }
    private void showCatalog() {
        if(rows==null || tab!=1) return;
        rows.removeAllViews(); String filter=search.getText().toString().trim().toLowerCase();
        for(int i=0;i<catalog.length();i++) {
            JSONObject item=catalog.optJSONObject(i); if(item==null) continue;
            final int id=item.optInt("id"); final String name=item.optString("name");
            if(!(name+id).toLowerCase().contains(filter)) continue;
            String state=item.optBoolean("owned")?"已拥有":"未解锁";
            if(kind.equals("skin") && item.optInt("condition")==2) state+=" · 觉醒外观";
            TextView row=text(name+"   #"+id+"   ·   "+state,12,item.optBoolean("owned")?muted:ink);
            row.setPadding(dp(10),0,dp(10),0); row.setBackground(background(Color.rgb(27,39,55),6));
            row.setOnClickListener(v -> {selected=id; selection.setText("已选择："+name+"  #"+id);notice.setText("已选择："+name+"  #"+id);}); add(rows,row,-1,33,0);
        }
    }
    private void saveMessage(String message) {
        if(notice!=null) notice.setText(message);
        Toast.makeText(activity,message,Toast.LENGTH_LONG).show();
    }
    private void chooseSave(boolean export) {
        if(!state.optBoolean("connected")) { saveMessage("请先登录游戏"); return; }
        if(export) request("save/export",null,value -> {
            if(!value.optBoolean("ok")) {saveMessage(value.optString("message"));return;}
            launchPicker(value);
        });
        else launchPicker(null);
    }
    private void launchPicker(JSONObject snapshot) {
        try {
            Activity owner=activity;
            SavePicker picker=(SavePicker)owner.getFragmentManager().findFragmentByTag("x2_save_picker");
            if(picker==null) {
                picker=new SavePicker();
                owner.getFragmentManager().beginTransaction().add(picker,"x2_save_picker").commit();
                owner.getFragmentManager().executePendingTransactions();
            }
            picker.snapshot=snapshot;
            Intent intent=new Intent(snapshot==null?Intent.ACTION_OPEN_DOCUMENT:Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType(snapshot==null?"*/*":"application/octet-stream");
            if(snapshot!=null) intent.putExtra(Intent.EXTRA_TITLE,"X2_"+(snapshot.optBoolean("developer")?"developer":"normal")+"_"+System.currentTimeMillis()+".x2save");
            picker.startActivityForResult(intent,snapshot==null?701:702);
        } catch(Exception e) {saveMessage("无法打开系统文件选择器："+e.getMessage());}
    }
    public static final class SavePicker extends Fragment {
        JSONObject snapshot;
        public SavePicker() {}
        @Override public void onActivityResult(int request,int result,Intent data) {
            super.onActivityResult(request,result,data);
            final GMWindow gm=instance;
            if(gm==null || result!=Activity.RESULT_OK || data==null || data.getData()==null) return;
            final Activity owner=getActivity(); final android.net.Uri uri=data.getData();
            final JSONObject saved=snapshot; snapshot=null;
            if(request!=701 && request!=702) return;
            if(request==702 && saved==null) {gm.saveMessage("导出已取消，请重新导出");return;}
            io.execute(() -> {
                try {
                    if(request==702) {
                        try(OutputStream out=owner.getContentResolver().openOutputStream(uri,"wt")) {
                            if(out==null) throw new IOException("文件不可写");
                            out.write(saved.toString().getBytes("UTF-8"));
                        }
                        main.post(() -> gm.saveMessage("存档导出成功"));
                    } else {
                        ByteArrayOutputStream buffer=new ByteArrayOutputStream();
                        try(InputStream in=owner.getContentResolver().openInputStream(uri)) {
                            if(in==null) throw new IOException("文件不可读");
                            byte[] bytes=new byte[8192]; int n;
                            while((n=in.read(bytes))!=-1) {if(buffer.size()+n>900000) throw new IOException("存档文件过大");buffer.write(bytes,0,n);}
                        }
                        final JSONObject file=new JSONObject(buffer.toString("UTF-8"));
                        if(!"X2SAVE-V1".equals(file.optString("format")) || !file.has("developer") || file.optString("data").isEmpty()) throw new IOException("不是兼容的 X2 存档");
                        main.post(() -> {
                            if(gm.activity!=owner || owner.isFinishing()) return;
                            new AlertDialog.Builder(owner).setTitle("导入存档")
                                .setMessage("存档："+file.optString("name")+"\n类型："+(file.optBoolean("developer")?"开发者账号":"普通账号")+"\n覆盖当前账号，原存档将自动备份。导入后必须重启游戏。")
                                .setNegativeButton("取消",null).setPositiveButton("确认导入",(d,w) -> {
                                    gm.request("save/import","confirm=1&developer="+(file.optBoolean("developer")?1:0)+"&data="+file.optString("data"),value -> gm.saveMessage(value.optString("message")));
                                }).show();
                        });
                    }
                } catch(Exception e) {main.post(() -> gm.saveMessage("存档操作失败："+e.getMessage()));}
            });
        }
    }
    public void onActivityResumed(Activity value){attach(value);}
    public void onActivityPaused(Activity value){if(value==activity) detach();}
    public void onActivityDestroyed(Activity value){if(value==activity){detach();activity=null;}}
    public void onActivityCreated(Activity a,Bundle b){} public void onActivityStarted(Activity a){} public void onActivityStopped(Activity a){} public void onActivitySaveInstanceState(Activity a,Bundle b){}
}
