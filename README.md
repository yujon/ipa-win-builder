## 描叙
本工具用于在`win`系统上将`ipa`用个人免费证书重签名并安装到手机上。
## 使用步骤

1. 下载安装[itunes](https://support.apple.com/en-us/HT210384)和[icloud](https://support.apple.com/en-hk/HT204283)

2. 下载[MiniAppBuilder](https://github.com/yujon/ipa-win-builder/releases/)并解压

3. 将`iphone`手机用数据线连接到电脑上

4. 然后打开终端工具，执行以下命令，该命令将会从ipa重签名并安装到手机上

```sh
cd MiniappMacBuilder-v1.0.0
./MiniAppBuilder.exe {your apple account} {your apple password}  {ipaPath} 
# ./MiniAppBuilder.exe xxx xxx /a/b/demo.ipa

```

5. 到手机端打开：设置 -> 通用 -> VPN与设备管理，然后选择信任对应的签名apple账号