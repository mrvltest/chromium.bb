/*
 * Copyright (C) 2013 Bloomberg L.P. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/editing/commands/BlockCommand.h"

#include "core/dom/Document.h"
#include "core/dom/NodeTraversal.h"
#include "core/editing/EditingUtilities.h"
#include "core/html/HTMLElement.h"
#include "core/HTMLNames.h"

namespace blink {

using namespace HTMLNames;

static bool isTableCellOrRootEditable(const Node* node)
{
    return isTableCell(node) || (node && node->isRootEditableElement());
}

BlockCommand::BlockCommand(Document& document)
    : CompositeEditCommand(document)
{
}

void BlockCommand::formatBlockExtent(PassRefPtrWillBeRawPtr<Node> prpFirstNode, PassRefPtrWillBeRawPtr<Node> prpLastNode, Node* stayWithin, EditingState *editingState)
{
    RefPtrWillBeRawPtr<Node> currentNode = prpFirstNode;
    RefPtrWillBeRawPtr<Node> endNode = prpLastNode;

    while (currentNode->isDescendantOf(endNode.get()))
        endNode = endNode->lastChild();

    while (currentNode) {
        while (endNode->isDescendantOf(currentNode.get())) {
            ASSERT(currentNode->firstChild());
            currentNode = currentNode->firstChild();
        }

        RefPtrWillBeRawPtr<Node> firstSibling = currentNode;
        RefPtrWillBeRawPtr<Node> lastSibling = currentNode;

        while (lastSibling != endNode) {
            RefPtrWillBeRawPtr<Node> nextSibling = lastSibling->nextSibling();
            if (!nextSibling || endNode->isDescendantOf(nextSibling.get()))
                break;
            lastSibling = nextSibling;
        }

        RefPtrWillBeRawPtr<Node> nextNode = lastSibling == endNode ? 0 : NodeTraversal::nextSkippingChildren(*lastSibling, stayWithin);
        formatBlockSiblings(firstSibling, lastSibling, stayWithin, endNode.get(), editingState);
        currentNode = nextNode;
    }
}

void BlockCommand::formatBlockSiblings(PassRefPtrWillBeRawPtr<Node> prpFirstSibling, PassRefPtrWillBeRawPtr<Node> prpLastSibling, Node* stayWithin, Node* lastNode, EditingState *editingState)
{
    ASSERT_NOT_REACHED();
}

void BlockCommand::doApply(EditingState *editingState)
{
    VisiblePosition startOfSelection;
    VisiblePosition endOfSelection;
    RefPtrWillBeRawPtr<ContainerNode> startScope;
    RefPtrWillBeRawPtr<ContainerNode> endScope;
    int startIndex;
    int endIndex;

    if (!prepareForBlockCommand(startOfSelection, endOfSelection, startScope, endScope, startIndex, endIndex, true))
        return;
    formatSelection(startOfSelection, endOfSelection, editingState);
    finishBlockCommand(startScope, endScope, startIndex, endIndex);
}

void BlockCommand::formatSelection(const VisiblePosition& startOfSelection, const VisiblePosition& endOfSelection, EditingState *editingState)
{
    // might be null if the recursion below goes awry
    if (startOfSelection.isNull() || endOfSelection.isNull())
        return;

    Node* startEnclosingCell = enclosingNodeOfType(startOfSelection.deepEquivalent(), &isTableCell);
    Node* endEnclosingCell = enclosingNodeOfType(endOfSelection.deepEquivalent(), &isTableCell);

    if (startEnclosingCell != endEnclosingCell) {
        if (startEnclosingCell && (!endEnclosingCell || !endEnclosingCell->isDescendantOf(startEnclosingCell))) {
            VisiblePosition newEnd = createVisiblePosition(lastPositionInNode(startEnclosingCell));
            VisiblePosition nextStart = nextPositionOf(newEnd);
            while (isDisplayInsideTable(nextStart.deepEquivalent().anchorNode()))
                nextStart = nextPositionOf(nextStart);
            // TODO: fix recursion!
            formatSelection(startOfSelection, newEnd, editingState);
            formatSelection(nextStart, endOfSelection, editingState);
            return;
        }

        ASSERT(endEnclosingCell);

        VisiblePosition nextStart = createVisiblePosition(firstPositionInNode(endEnclosingCell));
        VisiblePosition newEnd = previousPositionOf(nextStart);
        while (isDisplayInsideTable(newEnd.deepEquivalent().anchorNode()))
            newEnd = previousPositionOf(newEnd);
        // TODO: fix recursion!
        formatSelection(startOfSelection, newEnd, editingState);
        formatSelection(nextStart, endOfSelection, editingState);
        return;
    }


    Node* root = enclosingNodeOfType(startOfSelection.deepEquivalent(), &isTableCellOrRootEditable);
    if (!root || root == startOfSelection.deepEquivalent().anchorNode())
        return;

    RefPtrWillBeRawPtr<Node> currentNode = blockExtentStart(startOfSelection.deepEquivalent().anchorNode(), root);
    RefPtrWillBeRawPtr<Node> endNode = blockExtentEnd(endOfSelection.deepEquivalent().anchorNode(), root);

    while (currentNode->isDescendantOf(endNode.get()))
        endNode = endNode->lastChild();

    formatBlockExtent(currentNode, endNode, root, editingState);
}

}
