#pragma once
#include <Siv3D.hpp> // Siv3D v0.6.15

enum class MarkType
{
	Unassigned,
	ShopName,
	Date,
	Goods,
	Price,
	Number,
	Ignore,
};

enum Label
{
	ItemName,
	ItemPrice,
	ShopName,
	BuyDate,
	RegisterDate,
	ReceiptID,
	Size,
};

constexpr TileButton::Palette Palette1{
	.tileColor1 = Palette::Gainsboro,
	.tileColor2 = Color{ 33, 33, 33 },
	.borderColor1 = Palette::Black,
	.borderColor2 = Palette::White,
};
