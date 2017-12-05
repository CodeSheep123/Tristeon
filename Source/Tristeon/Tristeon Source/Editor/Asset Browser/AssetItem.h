#pragma once
#include "FileItem.h"
#include "Editor/TypeRegister.h"


namespace Tristeon
{
	namespace Editor
	{
		/**
		 * \brief An assetitem is any file that can be shown by the editor which is not a folder
		 */
		class AssetItem : public FileItem
		{
		public:
			AssetItem();
			~AssetItem();

			nlohmann::json serialize() override;
			void deserialize(nlohmann::json json) override;

			/**
			* \brief initialization is responsible for creating the parent relationships of the folder it is in,
			* creating the metadata and setting file information (eg. name, extension).
			* \param name The name of the file
			* \param folder The folder the file is contained in
			* \param extension The file extension of the file
			*/
			void init(std::string name, FolderItem* folder, std::string extension = "") override;
			
			/**
			 * \brief Creates the actual file, must be called after init
			 * \param json The serialized data to create the file with
			 */
			void createFile(nlohmann::json json);

			/**
			* \brief Moves the file to a new filepath
			*/
			void move(FolderItem* destination) override;
			
			/**
			* \brief Remove the file including the meta file
			*/
			void removeFile() override;
			virtual void onDoubleClick();

			std::string extension;
		private:
			static DerivedRegister<AssetItem> reg;
		};
	}
}
