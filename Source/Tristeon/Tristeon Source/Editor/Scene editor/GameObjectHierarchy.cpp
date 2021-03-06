﻿#include "GameObjectHierarchy.h"
#include "Editor/Asset Browser/AssetBrowser.h"
#include "Core/GameObject.h"
#include "Editor/EditorSelection.h"
#include <GLFW/glfw3.h>
#include "Scenes/SceneManager.h"
#include "Editor/EditorDragging.h"

using namespace Tristeon::Editor;

GameObjectHierarchy::GameObjectHierarchy()
{
}


GameObjectHierarchy::~GameObjectHierarchy()
{
}

void GameObjectHierarchy::drawNode(EditorNode* node)
{
	//Retrieve data
	nlohmann::json nodeData = *node->getData();

	//Type check
	if (nodeData["typeID"] != typeid(Core::GameObject).name())
	{
		std::cout << "Editor node for the GameObject hierarchy is not a GameObject, b-b-baka~!\n";
		return;
	}

	//Node flag conditions
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (node->children.size() == 0) flags |= ImGuiTreeNodeFlags_Leaf;
	if (node == selectedNode)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	//Load gui elements
	const std::string nodeName = nodeData["name"];
	const bool nodeOpen = ImGui::TreeNodeEx(nodeName.c_str(), flags);

	//Check if the user pressed the window and we need to unselect the curretly selected node
	if (ImGui::IsMouseClicked(0) && selectedNode == node && ImGui::IsWindowHovered())
	{
		selectedNode = nullptr;
	}

	//If the ui item is pressed select it
	if (ImGui::IsItemClicked() || ImGui::IsItemClicked(1))
	{
		std::cout << "Node: " << nodeData["name"] << " was selected\n";
		EditorSelection::setSelectedItem(node);
		selectedNode = node;
	}
	//Is the item hovered and is the mouse dragging?
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseClicked(0,false)) EditorDragging::setDragableItem(node);

	//Check if a dragging node is being dropped
	EditorNode* draggingNode = dynamic_cast<EditorNode*>(EditorDragging::getDragableItem());
	const bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	if (isHovered) hoveredNode = node;
	const bool isMouseReleased = ImGui::IsMouseReleased(0);
	bool hasChild = false;
	if (draggingNode != nullptr) hasChild = draggingNode->hasChild(node);

	//If dragging node is not null and the it is hovered above the last UI element it
	if (draggingNode != nullptr && draggingNode != node && !hasChild && isHovered && isMouseReleased)
	{
		draggingNode->move(node);
		EditorDragging::reset();
	}

	if (nodeOpen)
	{
		for (EditorNode* childNode : node->children)
			drawNode(childNode);
		ImGui::TreePop();
	}
}

void GameObjectHierarchy::loadScene(Tristeon::Scenes::Scene& scene)
{
	//Load all gameobjects to the gameobject hierarchy
	editorNodeTree.load(scene.serialize()["gameObjects"]);
	//Create parent relationships
	editorNodeTree.createParentalBonds();
}

void GameObjectHierarchy::onGui()
{
	checkSceneChanges();

	ImGui::Begin("Hierarchy", 0, windowFlags);

	//Temporary Test code
	if (ImGui::IsKeyPressed(GLFW_KEY_T, false) && ImGui::IsWindowHovered())
	{
		Core::GameObject* gameObject = new Core::GameObject();
		Scenes::SceneManager::getActiveScene()->addGameObject(gameObject);
		EditorNode* createdNode = new EditorNode(gameObject);
		gameObject->name = "Test";
		gameObject->tag = "Just a tag";
		createdNode->load(gameObject->serialize());
		if (selectedNode != nullptr &&
			(*selectedNode->getData())["typeID"] == typeid(Core::GameObject).name())
		{
			createdNode->parent = selectedNode;
			selectedNode->children.push_back(createdNode);
		}
		else editorNodeTree.nodes.push_back(createdNode);
		selectedNode = createdNode;
		EditorSelection::setSelectedItem(createdNode);
		ImGui::CloseCurrentPopup();
	}

	//Right click popup for gameobject creation
	if (ImGui::BeginPopupContextWindow("Asset creation"))
	{
		//GUI text for object name
		ImGui::InputText("Gameobject name", createdGameObjectName, 255);
		if (ImGui::Button("Create gameobject") || ImGui::IsKeyPressed(257, false))
		{
			//Add a new gameobject to the current scene
			Core::GameObject* gameObject = new Core::GameObject();
			Scenes::SceneManager::getActiveScene()->addGameObject(gameObject);

			//Load gameobject into an editorNode
			EditorNode* createdNode = new EditorNode(gameObject);
			gameObject->name = createdGameObjectName;
			createdNode->load(gameObject->serialize());

			//If a node has been selected use it as the new parent of the created gameobject
			if (selectedNode != nullptr)
			{
				createdNode->parent = selectedNode;
				selectedNode->children.push_back(createdNode);
			}
			editorNodeTree.nodes.push_back(createdNode);
			selectedNode = createdNode;
			EditorSelection::setSelectedItem(createdNode);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	//Draw nodes
	for (int i = 0; i < editorNodeTree.nodes.size(); i++)
	{
		if (editorNodeTree.nodes[i]->parent == nullptr) drawNode(editorNodeTree.nodes[i]);
	}

	EditorNode* draggingNode = dynamic_cast<EditorNode*>(EditorDragging::getDragableItem());
	//Drop dragged item outside of any gameobject, removing the parental bond
	if (ImGui::IsMouseReleased(0) && draggingNode != nullptr && hoveredNode == nullptr && draggingNode->parent != nullptr && ImGui::IsWindowHovered())
	{
		draggingNode->move(nullptr);
		EditorDragging::reset();
	}

	hoveredNode = nullptr;

	//Reset
	ImGui::End();
}

void GameObjectHierarchy::checkSceneChanges()
{
	if (currentScene == nullptr || currentScene == NULL) currentScene = Scenes::SceneManager::getActiveScene();
	else if (currentScene != nullptr && currentScene != Scenes::SceneManager::getActiveScene())
	{
		loadScene(*Scenes::SceneManager::getActiveScene());
		currentScene = Scenes::SceneManager::getActiveScene();
	}
}
