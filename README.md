Drop it in the Plugins directory of your Unreal Project and compile
Enable it from Plugins in Unreal Editor
Open a Chaos Cloth Asset and add the node UniversalClothSelectionRemap

In the example we connect the Source Collection pin to the Selection node (SimBackCloth) and Collection pin
to a MergeClothCollections nodecontaining the skeletal mesh imports of both the render mesh and proxy mesh
which are the MHC fitted assets. The final Collection output pin is connected to the Proxy Deformer node Collection input pin

<img width="1920" height="1085" alt="Example" src="https://github.com/user-attachments/assets/516bbea9-c51d-4cf6-82ba-b27f0eb0db21" />
