This plugin was created to remap the original asset's render/sim selection onto MHC fitted garments
First its important to make sure you run the MHC fitting on both the render mesh and the proxy mesh
to get the required skeletal meshes for this plugin to use.

Usage:
Drop it in the Plugins directory of your Unreal Project and compile
Enable it from Plugins in Unreal Editor
Open a Chaos Cloth Asset and add the node UniversalClothSelectionRemap

Examples below show the original asset's fitting issues when using a MHC character of different proportions.
While it works fine with a static mesh because it is fitted by MHC, with the cloth asset it does not

<img width="3840" height="2064" alt="ScreenShot00183" src="https://github.com/user-attachments/assets/6f9a0c46-8b70-416b-84fd-af7e831c737d" />
<img width="3840" height="2064" alt="ScreenShot00182" src="https://github.com/user-attachments/assets/d0c0fe45-4383-4674-8045-24f77d2ba079" />

In the example we connect the Source Collection pin to the Selection node (SimBackCloth) and Collection pin
to a MergeClothCollections nodecontaining the skeletal mesh imports of both the render mesh and proxy mesh
which are the MHC fitted assets. The final Collection output pin is connected to the Proxy Deformer node Collection input pin

<img width="1920" height="1085" alt="Example" src="https://github.com/user-attachments/assets/516bbea9-c51d-4cf6-82ba-b27f0eb0db21" />

The resulting cloth asset deforms properly and is fitted correctly

<img width="3840" height="2064" alt="ScreenShot00184" src="https://github.com/user-attachments/assets/2db7308f-398a-4c8e-b9b7-224075e83bd9" />
<img width="3840" height="2064" alt="ScreenShot00185" src="https://github.com/user-attachments/assets/4cd3864f-41a1-44f0-9e7d-132be5f37cd9" />

