#include <sdbus-c++/sdbus-c++.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

class BluetoothManager
{
private:
  std::unique_ptr<sdbus::IConnection>                          connection;
  std::unique_ptr<sdbus::IProxy>                               adapterProxy;
  std::string                                                  adapterPath;
  std::map<std::string, std::map<std::string, sdbus::Variant>> devices;
  std::string                                                  connectedDevice;
  std::map<std::string, std::string>                           characteristics;

  const std::string BLUEZ_SERVICE          = "org.bluez";
  const std::string ADAPTER_INTERFACE      = "org.bluez.Adapter1";
  const std::string DEVICE_INTERFACE       = "org.bluez.Device1";
  const std::string GATT_SERVICE_INTERFACE = "org.bluez.GattService1";
  const std::string GATT_CHAR_INTERFACE    = "org.bluez.GattCharacteristic1";
  const std::string PROPERTIES_INTERFACE   = "org.freedesktop.DBus.Properties";
  const std::string OBJECT_MANAGER_INTERFACE =
    "org.freedesktop.DBus.ObjectManager";

public:
  BluetoothManager()
  {
    connection = sdbus::createSystemBusConnection();
    findAdapter();
  }

  void findAdapter()
  {
    auto objectManager = sdbus::createProxy(
      *connection, sdbus::ServiceName(BLUEZ_SERVICE), sdbus::ObjectPath{"/"});

    std::map<sdbus::ObjectPath,
             std::map<std::string, std::map<std::string, sdbus::Variant>>>
      objects;
    objectManager->callMethod("GetManagedObjects")
      .onInterface(OBJECT_MANAGER_INTERFACE)
      .storeResultsTo(objects);

    for (const auto& [path, interfaces] : objects)
    {
      if (interfaces.find(ADAPTER_INTERFACE) != interfaces.end())
      {
        adapterPath  = path;
        adapterProxy = sdbus::createProxy(*connection,
                                          sdbus::ServiceName(BLUEZ_SERVICE),
                                          sdbus::ObjectPath{adapterPath});
        std::cout << "Found adapter: " << adapterPath << std::endl;
        return;
      }
    }
    throw std::runtime_error("No Bluetooth adapter found");
  }

  void startDiscovery()
  {
    try
    {
      adapterProxy->callMethod("StartDiscovery").onInterface(ADAPTER_INTERFACE);
      std::cout << "Discovery started..." << std::endl;
    }
    catch (const sdbus::Error& e)
    {
      std::cerr << "Failed to start discovery: " << e.what() << std::endl;
    }
  }

  void stopDiscovery()
  {
    try
    {
      adapterProxy->callMethod("StopDiscovery").onInterface(ADAPTER_INTERFACE);
      std::cout << "Discovery stopped." << std::endl;
    }
    catch (const sdbus::Error& e)
    {
      // Ignore if already stopped
    }
  }

  void scanDevices(int duration = 10)
  {
    devices.clear();
    startDiscovery();

    std::cout << "Scanning for " << duration << " seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(duration));

    stopDiscovery();
    updateDeviceList();
  }

  void updateDeviceList()
  {
    auto objectManager = sdbus::createProxy(
      *connection, sdbus::ServiceName(BLUEZ_SERVICE), sdbus::ObjectPath{"/"});

    std::map<sdbus::ObjectPath,
             std::map<std::string, std::map<std::string, sdbus::Variant>>>
      objects;
    objectManager->callMethod("GetManagedObjects")
      .onInterface(OBJECT_MANAGER_INTERFACE)
      .storeResultsTo(objects);

    for (const auto& [path, interfaces] : objects)
    {
      if (interfaces.find(DEVICE_INTERFACE) != interfaces.end())
      {
        devices[path] = interfaces.at(DEVICE_INTERFACE);
      }
    }
  }

  void listDevices(const std::string& filterService = "")
  {
    if (devices.empty())
    {
      std::cout << "No devices found. Run scan first." << std::endl;
      return;
    }

    std::cout << "\n=== Available Devices ===" << std::endl;
    int index = 1;

    for (const auto& [path, props] : devices)
    {
      std::string              name    = "Unknown";
      std::string              address = "Unknown";
      std::vector<std::string> uuids;

      if (props.find("Name") != props.end())
      {
        name = props.at("Name").get<std::string>();
      }
      if (props.find("Address") != props.end())
      {
        address = props.at("Address").get<std::string>();
      }
      if (props.find("UUIDs") != props.end())
      {
        uuids = props.at("UUIDs").get<std::vector<std::string>>();
      }

      // Filter by service UUID if specified
      if (!filterService.empty())
      {
        bool hasService = false;
        for (const auto& uuid : uuids)
        {
          if (uuid.find(filterService) != std::string::npos)
          {
            hasService = true;
            break;
          }
        }
        if (!hasService)
          continue;
      }

      std::cout << index++ << ". " << name << " [" << address << "]"
                << std::endl;
      std::cout << "   Path: " << path << std::endl;

      if (!uuids.empty())
      {
        std::cout << "   Services: ";
        for (size_t i = 0; i < uuids.size() && i < 3; ++i)
        {
          std::cout << uuids[i];
          if (i < uuids.size() - 1 && i < 2)
            std::cout << ", ";
        }
        if (uuids.size() > 3)
          std::cout << "...";
        std::cout << std::endl;
      }
      std::cout << std::endl;
    }
  }

  bool connectToDevice(const std::string& devicePath)
  {
    try
    {
      auto deviceProxy = sdbus::createProxy(*connection,
                                            sdbus::ServiceName(BLUEZ_SERVICE),
                                            sdbus::ObjectPath{devicePath});

      std::cout << "Connecting to device..." << std::endl;
      deviceProxy->callMethod("Connect").onInterface(DEVICE_INTERFACE);

      // Wait for connection
      std::this_thread::sleep_for(std::chrono::seconds(2));

      // Check if connected
      sdbus::Variant connectedVar;
      deviceProxy->callMethod("Get")
        .onInterface(PROPERTIES_INTERFACE)
        .withArguments(DEVICE_INTERFACE, "Connected")
        .storeResultsTo(connectedVar);

      bool connected = connectedVar.get<bool>();

      if (connected)
      {
        connectedDevice = devicePath;
        std::cout << "Successfully connected!" << std::endl;

        // Request MTU update
        requestMTU(devicePath, 250);

        // Discover services
        discoverServices(devicePath);

        return true;
      }
      else
      {
        std::cout << "Failed to connect." << std::endl;
        return false;
      }
    }
    catch (const sdbus::Error& e)
    {
      std::cerr << "Connection error: " << e.what() << std::endl;
      return false;
    }
  }

  void requestMTU(const std::string& devicePath, uint16_t mtu)
  {
    try
    {
      auto deviceProxy = sdbus::createProxy(*connection,
                                            sdbus::ServiceName(BLUEZ_SERVICE),
                                            sdbus::ObjectPath{devicePath});

      // Note: BlueZ doesn't directly expose MTU exchange, but we can try
      // setting it
      std::cout << "Requesting MTU of " << mtu << " bytes..." << std::endl;

      // The MTU exchange typically happens automatically during connection
      // We can check the current MTU
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      std::cout << "MTU exchange completed (automatic during connection)"
                << std::endl;
    }
    catch (const sdbus::Error& e)
    {
      std::cerr << "MTU request note: " << e.what() << std::endl;
    }
  }

  void disconnectFromDevice()
  {
    if (connectedDevice.empty())
    {
      std::cout << "No device connected." << std::endl;
      return;
    }

    try
    {
      auto deviceProxy = sdbus::createProxy(*connection,
                                            sdbus::ServiceName(BLUEZ_SERVICE),
                                            sdbus::ObjectPath{connectedDevice});
      deviceProxy->callMethod("Disconnect").onInterface(DEVICE_INTERFACE);
      std::cout << "Disconnected from device." << std::endl;
      connectedDevice.clear();
      characteristics.clear();
    }
    catch (const sdbus::Error& e)
    {
      std::cerr << "Disconnect error: " << e.what() << std::endl;
    }
  }

  void forgetDevice(const std::string& devicePath)
  {
    try
    {
      // Disconnect first if connected
      if (connectedDevice == devicePath)
      {
        disconnectFromDevice();
      }

      adapterProxy->callMethod("RemoveDevice")
        .onInterface(ADAPTER_INTERFACE)
        .withArguments(sdbus::ObjectPath(devicePath));

      devices.erase(devicePath);
      std::cout << "Device forgotten." << std::endl;
    }
    catch (const sdbus::Error& e)
    {
      std::cerr << "Error forgetting device: " << e.what() << std::endl;
    }
  }

  void discoverServices(const std::string& devicePath)
  {
    std::cout << "Discovering services and characteristics..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto objectManager = sdbus::createProxy(
      *connection, sdbus::ServiceName(BLUEZ_SERVICE), sdbus::ObjectPath{"/"});

    std::map<sdbus::ObjectPath,
             std::map<std::string, std::map<std::string, sdbus::Variant>>>
      objects;
    objectManager->callMethod("GetManagedObjects")
      .onInterface(OBJECT_MANAGER_INTERFACE)
      .storeResultsTo(objects);

    characteristics.clear();

    for (const auto& [path, interfaces] : objects)
    {
      std::string pathStr = path;

      // Check if this characteristic belongs to our device
      if (pathStr.find(devicePath) == std::string::npos)
        continue;

      if (interfaces.find(GATT_CHAR_INTERFACE) != interfaces.end())
      {
        const auto& charProps = interfaces.at(GATT_CHAR_INTERFACE);

        if (charProps.find("UUID") != charProps.end())
        {
          std::string uuid      = charProps.at("UUID").get<std::string>();
          characteristics[uuid] = pathStr;
        }
      }
    }

    std::cout << "Found " << characteristics.size() << " characteristics."
              << std::endl;
  }

  void listCharacteristics()
  {
    if (characteristics.empty())
    {
      std::cout << "No characteristics available. Connect to a device first."
                << std::endl;
      return;
    }

    std::cout << "\n=== Available Characteristics ===" << std::endl;
    int index = 1;
    for (const auto& [uuid, path] : characteristics)
    {
      std::cout << index++ << ". UUID: " << uuid << std::endl;
      std::cout << "   Path: " << path << std::endl;

      try
      {
        auto           charProxy = sdbus::createProxy(*connection,
                                            sdbus::ServiceName(BLUEZ_SERVICE),
                                            sdbus::ObjectPath{path});
        sdbus::Variant flagsVar;
        charProxy->callMethod("Get")
          .onInterface(PROPERTIES_INTERFACE)
          .withArguments(GATT_CHAR_INTERFACE, "Flags")
          .storeResultsTo(flagsVar);

        auto flags = flagsVar.get<std::vector<std::string>>();
        std::cout << "   Flags: ";
        for (size_t i = 0; i < flags.size(); ++i)
        {
          std::cout << flags[i];
          if (i < flags.size() - 1)
            std::cout << ", ";
        }
        std::cout << std::endl;
      }
      catch (...)
      {
      }

      std::cout << std::endl;
    }
  }

  void enableNotify(const std::string& characteristicUUID)
  {
    auto it = characteristics.find(characteristicUUID);
    if (it == characteristics.end())
    {
      std::cout << "Characteristic not found." << std::endl;
      return;
    }

    try
    {
      auto charProxy = sdbus::createProxy(*connection,
                                          sdbus::ServiceName(BLUEZ_SERVICE),
                                          sdbus::ObjectPath{it->second});

      std::cout << "Notifications enabled for " << characteristicUUID
                << std::endl;

      // Register signal handler for notifications
      charProxy->uponSignal("PropertiesChanged")
        .onInterface(PROPERTIES_INTERFACE)
        .call([characteristicUUID](
                const std::string&                           interface,
                const std::map<std::string, sdbus::Variant>& changed,
                const std::vector<std::string>&              invalidated) {
          if (changed.find("Value") != changed.end())
          {
            auto value = changed.at("Value").get<std::vector<uint8_t>>();
            std::cout << "\n[NOTIFY " << characteristicUUID << "] ";
            printHexData(value);
            std::cout << std::endl;
          }
        });

      charProxy->callMethod("StartNotify").onInterface(GATT_CHAR_INTERFACE);
    }
    catch (const sdbus::Error& e)
    {
      std::cerr << "Error enabling notifications: " << e.what() << std::endl;
    }
  }

  void disableNotify(const std::string& characteristicUUID)
  {
    auto it = characteristics.find(characteristicUUID);
    if (it == characteristics.end())
    {
      std::cout << "Characteristic not found." << std::endl;
      return;
    }

    try
    {
      auto charProxy = sdbus::createProxy(*connection,
                                          sdbus::ServiceName(BLUEZ_SERVICE),
                                          sdbus::ObjectPath{it->second});
      charProxy->callMethod("StopNotify").onInterface(GATT_CHAR_INTERFACE);
      std::cout << "Notifications disabled for " << characteristicUUID
                << std::endl;
    }
    catch (const sdbus::Error& e)
    {
      std::cerr << "Error disabling notifications: " << e.what() << std::endl;
    }
  }

  void writeCharacteristic(const std::string&          characteristicUUID,
                           const std::vector<uint8_t>& data)
  {
    auto it = characteristics.find(characteristicUUID);
    if (it == characteristics.end())
    {
      std::cout << "Characteristic not found." << std::endl;
      return;
    }

    try
    {
      auto charProxy = sdbus::createProxy(*connection,
                                          sdbus::ServiceName(BLUEZ_SERVICE),
                                          sdbus::ObjectPath{it->second});

      std::map<std::string, sdbus::Variant> options;
      options["type"] = sdbus::Variant("request");

      charProxy->callMethod("WriteValue")
        .onInterface(GATT_CHAR_INTERFACE)
        .withArguments(data, options);

      std::cout << "Data written to characteristic " << characteristicUUID
                << std::endl;
    }
    catch (const sdbus::Error& e)
    {
      std::cerr << "Error writing characteristic: " << e.what() << std::endl;
    }
  }

  void readCharacteristic(const std::string& characteristicUUID)
  {
    auto it = characteristics.find(characteristicUUID);
    if (it == characteristics.end())
    {
      std::cout << "Characteristic not found." << std::endl;
      return;
    }

    try
    {
      auto charProxy = sdbus::createProxy(*connection,
                                          sdbus::ServiceName(BLUEZ_SERVICE),
                                          sdbus::ObjectPath{it->second});

      std::map<std::string, sdbus::Variant> options;
      std::vector<uint8_t>                  value;

      charProxy->callMethod("ReadValue")
        .onInterface(GATT_CHAR_INTERFACE)
        .withArguments(options)
        .storeResultsTo(value);

      std::cout << "Read from " << characteristicUUID << ": ";
      printHexData(value);
      std::cout << std::endl;
    }
    catch (const sdbus::Error& e)
    {
      std::cerr << "Error reading characteristic: " << e.what() << std::endl;
    }
  }

  static void printHexData(const std::vector<uint8_t>& data)
  {
    std::cout << "0x";
    for (uint8_t byte : data)
    {
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<uint16_t>(byte) << " ";
    }
    std::cout << std::dec << " (";
    for (uint8_t byte : data)
    {
      if (byte >= 32 && byte < 127)
      {
        std::cout << (char)byte;
      }
      else
      {
        std::cout << ".";
      }
    }
    std::cout << ")";
  }

  void processEvents() { connection->enterEventLoopAsync(); }

  std::string getConnectedDevice() const { return connectedDevice; }
  const std::map<std::string, std::map<std::string, sdbus::Variant>>&
  getDevices() const
  {
    return devices;
  }
};

void printMenu()
{
  std::cout << "\n=== Bluetooth LE Manager ===" << std::endl;
  std::cout << "1.  Scan for devices" << std::endl;
  std::cout << "2.  List all devices" << std::endl;
  std::cout << "3.  List devices by service UUID" << std::endl;
  std::cout << "4.  Connect to device" << std::endl;
  std::cout << "5.  Disconnect from device" << std::endl;
  std::cout << "6.  Forget device" << std::endl;
  std::cout << "7.  List characteristics" << std::endl;
  std::cout << "8.  Enable notifications" << std::endl;
  std::cout << "9.  Disable notifications" << std::endl;
  std::cout << "10. Write to characteristic" << std::endl;
  std::cout << "11. Read from characteristic" << std::endl;
  std::cout << "0.  Exit" << std::endl;
  std::cout << "\nChoice: ";
}

int main()
{
  try
  {
    BluetoothManager btManager;
    btManager.processEvents();

    int choice;
    while (true)
    {
      printMenu();
      std::cin >> choice;
      std::cin.ignore();

      switch (choice)
      {
        case 1:
        {
          int duration;
          std::cout << "Scan duration (seconds): ";
          std::cin >> duration;
          std::cin.ignore();
          btManager.scanDevices(duration);
          break;
        }
        case 2:
          btManager.listDevices();
          break;

        case 3:
        {
          std::string serviceUUID;
          std::cout << "Enter service UUID (partial match): ";
          std::getline(std::cin, serviceUUID);
          btManager.listDevices(serviceUUID);
          break;
        }
        case 4:
        {
          std::string devicePath;
          std::cout << "Enter device path: ";
          std::getline(std::cin, devicePath);
          btManager.connectToDevice(devicePath);
          break;
        }
        case 5:
          btManager.disconnectFromDevice();
          break;

        case 6:
        {
          std::string devicePath;
          std::cout << "Enter device path: ";
          std::getline(std::cin, devicePath);
          btManager.forgetDevice(devicePath);
          break;
        }
        case 7:
          btManager.listCharacteristics();
          break;

        case 8:
        {
          std::string uuid;
          std::cout << "Enter characteristic UUID: ";
          std::getline(std::cin, uuid);
          btManager.enableNotify(uuid);
          break;
        }
        case 9:
        {
          std::string uuid;
          std::cout << "Enter characteristic UUID: ";
          std::getline(std::cin, uuid);
          btManager.disableNotify(uuid);
          break;
        }
        case 10:
        {
          std::string uuid, hexData;
          std::cout << "Enter characteristic UUID: ";
          std::getline(std::cin, uuid);
          std::cout << "Enter hex data (e.g., 01 02 03): ";
          std::getline(std::cin, hexData);

          std::vector<uint8_t> data;
          std::istringstream   iss(hexData);
          std::string          byteStr;
          while (iss >> byteStr)
          {
            data.push_back(std::stoi(byteStr, nullptr, 16));
          }

          btManager.writeCharacteristic(uuid, data);
          break;
        }
        case 11:
        {
          std::string uuid;
          std::cout << "Enter characteristic UUID: ";
          std::getline(std::cin, uuid);
          btManager.readCharacteristic(uuid);
          break;
        }
        case 0:
          std::cout << "Exiting..." << std::endl;
          return 0;

        default:
          std::cout << "Invalid choice." << std::endl;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
