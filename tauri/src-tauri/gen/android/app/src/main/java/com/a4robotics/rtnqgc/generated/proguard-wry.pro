# THIS FILE IS AUTO-GENERATED. DO NOT MODIFY!!

# Copyright 2020-2023 Tauri Programme within The Commons Conservancy
# SPDX-License-Identifier: Apache-2.0
# SPDX-License-Identifier: MIT

-keep class com.a4robotics.rtnqgc.* {
  native <methods>;
}

-keep class com.a4robotics.rtnqgc.WryActivity {
  public <init>(...);

  void setWebView(com.a4robotics.rtnqgc.RustWebView);
  java.lang.Class getAppClass(...);
  int getId();
  java.lang.String getVersion();
  int startActivity(...);
}

-keep class com.a4robotics.rtnqgc.Ipc {
  public <init>(...);

  @android.webkit.JavascriptInterface public <methods>;
}

-keep class com.a4robotics.rtnqgc.RustWebView {
  public <init>(...);

  void loadUrlMainThread(...);
  void loadHTMLMainThread(...);
  void evalScript(...);
}

-keep class com.a4robotics.rtnqgc.RustWebChromeClient,com.a4robotics.rtnqgc.RustWebViewClient {
  public <init>(...);
}
