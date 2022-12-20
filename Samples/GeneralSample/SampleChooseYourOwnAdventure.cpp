
// Pills that supress your super power
// Told they were for your weak immune system
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <IBEngine/IBCollection.h>

struct TextNode;
struct TextOption
{
    TextNode const* Next = nullptr;
    char const* Input = nullptr;
};

struct TextNode
{
    char const *Text = nullptr;
    IB::Span<TextOption const> Options = {};
};

extern TextNode const Entry;
TextOption const PlayAgainOptions[]
{
    { &Entry, "yY"},
    { nullptr, "nN"}
};

TextNode const Supressed =
{
    "Yup, they're still supressed.\n"
    "Play Again?\n"
    "Y? N?\n",
    IB::asSpan(PlayAgainOptions)
};

TextNode const NotSupressed =
{
    "Yup, they're not supressed.\n"
    "Play Again?"
    "Y? N?\n",
    IB::asSpan(PlayAgainOptions)
};

TextOption const EntryOptions[] =
{
    { &Supressed, "yY" },
    { &NotSupressed, "nN"}
};
TextNode const Entry =
{
    "You've discovered that the pills you've been taking are actually supressing your INCREDIBLE powers.\n"
    "Keep taking them?\n"
    "Y? N?\n",
    IB::asSpan(EntryOptions)
};

void sampleChooseYourOwnAdventure()
{
    TextNode const*iter = &Entry;

    while (iter != nullptr)
    {
        printf("%s", iter->Text);

        if(iter->Options.Count == 0)
        {
            break;
        }

        bool optionSelected = false;
        do
        {
            char input[2] = {};
            input[0] = getchar();
            while(getchar() != '\n') {}

            for(uint32_t i = 0; i < iter->Options.Count; i++)
            {
                if (strstr(iter->Options[i].Input, input) != nullptr)
                {
                    iter = iter->Options[i].Next;
                    optionSelected = true;
                    break;
                }
            }

            if(!optionSelected)
            {
                printf("I didn't understand that. Please try again.\n");
            }
        }while(!optionSelected);
    }
}
